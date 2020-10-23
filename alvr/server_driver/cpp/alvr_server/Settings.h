#pragma once

#include <openvr_driver.h>
#include "common-utils.h"
#include "Bitrate.h"
#include "Utils.h"
#include "FFR.h"

class Settings
{
	static Settings m_Instance;
	bool m_loaded;

	Settings();
	virtual ~Settings();

public:
	void Load();
	static Settings &Instance() {
		return m_Instance;
	}

	bool IsLoaded() {
		return m_loaded;
	}

	std::string mSerialNumber;
	std::string mTrackingSystemName;
	std::string mModelNumber;
	std::string mDriverVersion;
	std::string mManufacturerName;
	std::string mRenderModelName;
	std::string mRegisteredDeviceType;

	int32_t m_nAdapterIndex;

	uint64_t m_DriverTestMode = 0;

	int m_refreshRate;
	int32_t m_renderWidth;
	int32_t m_renderHeight;
	int32_t m_recommendedTargetWidth;
	int32_t m_recommendedTargetHeight;


	EyeFov m_eyeFov[2];
	float m_flSecondsFromVsyncToPhotons;
	float m_flIPD;

	bool m_enableFoveatedRendering;
	float m_foveationStrength;
	float m_foveationShape;
	float m_foveationVerticalOffset;

	bool m_enableColorCorrection;
	float m_brightness;
	float m_contrast;
	float m_saturation;
	float m_gamma;
	float m_sharpening;

	bool m_enableSound;
	std::string m_soundDevice;

	bool m_streamMic;
	std::string m_microphoneDevice;

	int m_codec;
	std::string m_EncoderOptions;
	Bitrate mEncodeBitrate;

	std::string m_Host;
	int m_Port;
	std::string m_ConnectedClient;
	Bitrate mThrottlingBitrate;


	uint64_t m_SendingTimeslotUs;
	uint64_t m_LimitTimeslotPackets;

	uint32_t m_clientRecvBufferSize;

	uint32_t m_frameQueueSize;

	bool m_force60HZ;

	bool m_nv12;

	// Controller configs
	std::string m_controllerTrackingSystemName;
	std::string m_controllerManufacturerName;
	std::string m_controllerModelNumber;
	std::string m_controllerRenderModelNameLeft;
	std::string m_controllerRenderModelNameRight;
	std::string m_controllerSerialNumber;
	std::string m_controllerType;
	std::string mControllerRegisteredDeviceType;
	std::string m_controllerInputProfilePath;
	bool m_disableController;
	int32_t m_controllerTriggerMode;
	int32_t m_controllerTrackpadClickMode;
	int32_t m_controllerTrackpadTouchMode;
	int32_t m_controllerBackMode;
	int32_t m_controllerRecenterButton;

	double m_controllerPoseOffset = 0;

	float m_OffsetPos[3];
	bool m_EnableOffsetPos;

	double m_leftControllerPositionOffset[3];
	double m_leftControllerRotationOffset[3];

	float m_hapticsIntensity;

	int32_t m_causePacketLoss;

	bool m_useTrackingReference;

	int32_t m_trackingFrameOffset;

	bool m_force3DOF;

	bool m_aggressiveKeyframeResend;

	// They are not in config json and set by "SetConfig" command.
	bool m_captureLayerDDSTrigger = false;
	bool m_captureComposedDDSTrigger = false;
	
	int m_controllerMode = 0;

	std::string m_appearance;
};

