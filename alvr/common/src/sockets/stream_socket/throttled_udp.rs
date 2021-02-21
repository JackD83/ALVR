use crate::{
    sockets::{StreamId, LOCAL_IP},
    *,
};
use bytes::{BufMut, Bytes, BytesMut};

use futures::{Stream, StreamExt};
use governor::{
    clock,
    state::{InMemoryState, NotKeyed},
    Quota, RateLimiter,
};
use nonzero_ext::NonZero;
use std::{
    collections::HashMap,
    io,
    mem::MaybeUninit,
    net::{IpAddr, SocketAddr},
    pin::Pin,
    sync::Arc,
    task::{Context, Poll},
};
use tokio::{
    io::ReadBuf,
    net::UdpSocket,
    sync::{mpsc, Mutex},
};

const INITIAL_RD_CAPACITY: usize = 64 * 1024;

// Don't go below this rate, limiting then is unneeded anyway.
const MINIMUM_BYTERATE: u32 = 30 * 1024 * 1024 * 3 / 2 / 8;
// Reserve includes audio along with other small fluctuations.
const RESERVE_BYTERATE: u32 = 5_000_000 / 8;

pub struct ThrottlingSettings {
    pub byterate: u32,
    pub multiplier: f32,
}

#[allow(clippy::type_complexity)]
#[derive(Clone)]
pub struct ThrottledUdpStreamSendSocket {
    inner: Arc<UdpSocket>,
    limiter: Arc<Option<RateLimiter<NotKeyed, InMemoryState, clock::DefaultClock>>>,
}

impl ThrottledUdpStreamSendSocket {
    pub async fn send(&self, data: Bytes) -> io::Result<()> {
        if let Some(ref limiter) = *self.limiter {
            if let Some(len) = NonZero::new(data.len() as u32) {
                limiter.until_n_ready(len).await.ok();
            }
        }
        match self.inner.send(&data).await {
            Ok(_) => Ok(()),
            Err(e) => Err(e),
        }
    }
}

pub struct ThrottledUdpStreamReceiveSocket {
    pub inner: Arc<UdpSocket>,
    buffer: BytesMut,
}

impl Stream for ThrottledUdpStreamReceiveSocket {
    type Item = io::Result<(BytesMut, SocketAddr)>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let pin = self.get_mut();

        pin.buffer.reserve(INITIAL_RD_CAPACITY);

        let addr = unsafe {
            let buf = &mut *(pin.buffer.chunk_mut() as *mut _ as *mut [MaybeUninit<u8>]);
            let mut read = ReadBuf::uninit(buf);
            let ptr = read.filled().as_ptr();
            let res = match Pin::new(&mut pin.inner).poll_recv_from(cx, &mut read) {
                Poll::Ready(res) => res,
                Poll::Pending => return Poll::Pending,
            };

            assert_eq!(ptr, read.filled().as_ptr());
            let addr = res?;
            pin.buffer.advance_mut(read.filled().len());
            addr
        };
        Poll::Ready(Some(Ok((pin.buffer.split_to(pin.buffer.len()), addr))))
    }
}

pub async fn connect(
    peer_ip: IpAddr,
    port: u16,
    throttling_settings: Option<ThrottlingSettings>,
) -> StrResult<(
    ThrottledUdpStreamSendSocket,
    ThrottledUdpStreamReceiveSocket,
)> {
    let peer_addr: SocketAddr = (peer_ip, port).into();
    let socket = trace_err!(UdpSocket::bind((LOCAL_IP, port)).await)?;
    trace_err!(socket.connect(peer_addr).await)?;

    let rx = Arc::new(socket);
    let tx = rx.clone();

    let limiter = match throttling_settings {
        Some(settings) => {
            // The byterate and burst amount computation here is based
            // on the previous C++ implementation.
            let byterate =
                (settings.byterate as f32 * settings.multiplier) as u32 + RESERVE_BYTERATE;
            let byterate = std::cmp::max(MINIMUM_BYTERATE, byterate);
            let burst = byterate / 1000;
            let quota = Quota::per_second(NonZero::new(byterate).unwrap())
                .allow_burst(NonZero::new(burst).unwrap());
            Some(RateLimiter::direct(quota))
        }
        None => None,
    };

    Ok((
        ThrottledUdpStreamSendSocket {
            inner: tx,
            limiter: Arc::new(limiter),
        },
        ThrottledUdpStreamReceiveSocket {
            inner: rx,
            buffer: BytesMut::new(),
        },
    ))
}

pub async fn receive_loop(
    mut socket: ThrottledUdpStreamReceiveSocket,
    packet_enqueuers: Arc<Mutex<HashMap<StreamId, mpsc::UnboundedSender<BytesMut>>>>,
) -> StrResult {
    while let Some(maybe_packet) = socket.next().await {
        let (packet_bytes, _) = trace_err!(maybe_packet)?;

        let stream_id = packet_bytes[0];
        if let Some(enqueuer) = packet_enqueuers.lock().await.get_mut(&stream_id) {
            trace_err!(enqueuer.send(packet_bytes))?;
        }
    }

    Ok(())
}
