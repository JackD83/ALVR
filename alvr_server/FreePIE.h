#pragma once

#include "ipctools.h"
#include "resource.h"
#include "packet_types.h"
#include "Utils.h"
#include "Logger.h"

class FreePIE
{
public:
	static const uint32_t ALVR_FREEPIE_SIGNATURE_V3 = 0x11223346;

	static const uint32_t ALVR_FREEPIE_FLAG_OVERRIDE_HEAD_ORIENTATION = 1 << 0;
	static const uint32_t ALVR_FREEPIE_FLAG_OVERRIDE_CONTROLLER_ORIENTATION0 = 1 << 1;
	static const uint32_t ALVR_FREEPIE_FLAG_OVERRIDE_HEAD_POSITION = 1 << 2;
	static const uint32_t ALVR_FREEPIE_FLAG_OVERRIDE_CONTROLLER_POSITION0 = 1 << 3;
	static const uint32_t ALVR_FREEPIE_FLAG_OVERRIDE_BUTTONS = 1 << 4;

	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_TRACKPAD_CLICK = 1 << 0;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_TRACKPAD_TOUCH = 1 << 1;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_BACK = 1 << 2;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_VOLUME_UP = 1 << 3;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_VOLUME_DOWN = 1 << 4;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_A = 1 << 5;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_B = 1 << 6;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_RTHUMB = 1 << 7;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_RSHOULDER = 1 << 8;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_X = 1 << 9;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_Y = 1 << 10;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_LTHUMB = 1 << 11;
	static const uint32_t ALVR_FREEPIE_INPUT_BUTTON_LSHOULDER = 1 << 12;

	static const uint32_t ALVR_FREEPIE_BUTTONS = 21;
	static const uint32_t ALVR_FREEPIE_MESSAGE_LENGTH = 512;

	static const int BUTTON_MAP[FreePIE::ALVR_FREEPIE_BUTTONS];

#pragma pack(push, 1)
	struct FreePIEFileMapping {
		uint32_t version;
		uint32_t flags;
		double input_head_orientation[3];
		double input_controller_orientation[2][3];
		double input_head_position[3];
		double input_controller_position[2][3];
		double input_trackpad[2][2];
		uint64_t inputControllerButtons[2];
		double input_haptic_feedback[2][3];
		uint32_t controllers;
		uint32_t controllerButtons[2];
		double head_orientation[3];
		double controller_orientation[2][3];
		double head_position[3];
		double controller_position[2][3];
		double trigger[2];
		double trigger_left[2];
		double trigger_right[2];
		double joystick_left[2][2];
		double joystick_right[2][2];
		double trackpad[2][2];
		char message[ALVR_FREEPIE_MESSAGE_LENGTH];
	};
#pragma pack(pop)

	FreePIE()
		: mFileMapping(ALVR_FREEPIE_FILEMAPPING_NAME, sizeof(FreePIEFileMapping))
		, mMutex(ALVR_FREEPIE_MUTEX_NAME) {
		Initialize();
	}
	~FreePIE() {
	}

	void UpdateTrackingInfoByFreePIE(const TrackingInfo &info, vr::HmdQuaternion_t &head_orientation
		, vr::HmdQuaternion_t controller_orientation[TrackingInfo::MAX_CONTROLLERS]
		, const TrackingVector3 &head_position
		, const TrackingVector3 controller_position[TrackingInfo::MAX_CONTROLLERS]
		, double haptic_feedback[2][3]) {
		mMutex.Wait();

		QuaternionToEulerAngle(head_orientation, mMapped->input_head_orientation);
		mMapped->input_head_position[0] = head_position.x;
		mMapped->input_head_position[1] = head_position.y;
		mMapped->input_head_position[2] = head_position.z;

		for (int i = 0; i < TrackingInfo::MAX_CONTROLLERS; i++) {
			QuaternionToEulerAngle(controller_orientation[i], mMapped->input_controller_orientation[i]);
			mMapped->input_controller_position[i][0] = controller_position[i].x;
			mMapped->input_controller_position[i][1] = controller_position[i].y;
			mMapped->input_controller_position[i][2] = controller_position[i].z;

			mMapped->input_trackpad[i][0] = info.controller[i].trackpadPosition.x;
			mMapped->input_trackpad[i][1] = info.controller[i].trackpadPosition.y;

			mMapped->inputControllerButtons[i] = info.controller[i].buttons;
		}

		// When client sends two controller information and FreePIE is not running, detect it here.
		if (info.controller[1].flags & TrackingInfo::Controller::FLAG_CONTROLLER_ENABLE) {
			mMapped->controllers = 2;
		}

		mMapped->message[ALVR_FREEPIE_MESSAGE_LENGTH - 1] = 0;

		mMapped->input_haptic_feedback[0][0] = haptic_feedback[0][0];
		mMapped->input_haptic_feedback[0][1] = haptic_feedback[0][1];
		mMapped->input_haptic_feedback[0][2] = haptic_feedback[0][2];
		mMapped->input_haptic_feedback[1][0] = haptic_feedback[1][0];
		mMapped->input_haptic_feedback[1][1] = haptic_feedback[1][1];
		mMapped->input_haptic_feedback[1][2] = haptic_feedback[1][2];

		memcpy(&mCopy, mMapped, sizeof(FreePIEFileMapping));

		mMutex.Release();
	}

	const FreePIEFileMapping& GetData() {
		return mCopy;
	}

private:
	void Initialize() {
		mMutex.Wait();

		mMapped = (FreePIEFileMapping *)mFileMapping.Map(FILE_MAP_WRITE);
		memset(mMapped, 0, sizeof(FreePIEFileMapping));
		mMapped->version = ALVR_FREEPIE_SIGNATURE_V3;
		mMapped->flags = 0;

		mMapped->controllers = 1;

		memcpy(&mCopy, mMapped, sizeof(FreePIEFileMapping));

		mMutex.Release();
	}

	IPCFileMapping mFileMapping;
	IPCMutex mMutex;

	FreePIEFileMapping *mMapped;
	FreePIEFileMapping mCopy;
};

