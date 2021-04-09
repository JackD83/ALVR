#pragma once

#include "alvr_server/IDRScheduler.h"
#include "shared/threadtools.h"
#include <atomic>
#include <sys/types.h>

class ClientConnection;
class PoseHistory;

class CEncoder : public CThread {
  public:
    CEncoder(std::shared_ptr<ClientConnection> listener, std::shared_ptr<PoseHistory> poseHistory);
    ~CEncoder();
    bool Init() override { return true; }
    void Run() override;

    void Stop();
    void OnPacketLoss();
    void InsertIDR();

  private:
    void GetFds(int client, int (*fds)[6]);
    // Find the index of pose (tracking information) used to render the current frame
    // returns false if there is no new frame since last call
    bool UpdatePoseIndex();

    std::shared_ptr<ClientConnection> m_listener;
    std::shared_ptr<PoseHistory> m_poseHistory;
    uint64_t m_poseSubmitIndex = 0;
    uint32_t m_lastFrame = 0;
    std::atomic_bool m_exiting{false};
    IDRScheduler m_scheduler;
    int m_socket;
    std::string m_socketPath;
    int m_fds[6];
};
