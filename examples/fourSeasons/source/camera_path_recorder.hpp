#pragma once

#include "quake_camera.hpp"
#include "timer_interface.hpp"

class camera_path_recorder
{
public:
	camera_path_recorder(avk::quake_camera& cam,
		float recording_density =  0.05f) // default recording density is 0.05 seconds
		: mCam{ &cam } // Target camera to track and record a path
		, mStartTime{avk::time().time_since_start()} // Set in initialize()
		, mLastRecordedTime{ 0.0f }
	   	, mRecordingDensity{ recording_density } // How dense should the recording be in ms between two recorded points
	{
		
	}

	void update()
	{
		if (mRecording) {
			auto t = (avk::time().time_since_start() - mStartTime);
			if (t - mLastRecordedTime >= mRecordingDensity) {
				mRecordingPathPositions->push_back(mCam->translation());
				// mRecordingPathRotations->push_back(mCam->rotation());

				//use the translation and rotation to get the direction vector (x,y,z)
				auto directionVector = glm::normalize(front(*mCam));
				mRecordingPathRotations->push_back(directionVector);
				
				
				mLastRecordedTime = t;
			}
		}
	}

	void start_recording()
	{
		mRecordingPathPositions = std::make_unique<std::vector<glm::vec3>>();
		mRecordingPathRotations = std::make_unique<std::vector<glm::vec3>>();
		mStartTime = avk::time().time_since_start();
		mRecording = true;
		mLastRecordedTime = 0.0f;
	}

	void stop_recording()
	{
		mRecording = false;
		save_to_disk("assets\\camera_path.txt");
	}

	void _save_to_disk(std::string path, std::unique_ptr<std::vector<glm::vec3>> mRecordingPathPositions, std::unique_ptr<std::vector<glm::vec3>> mRecordingPathRotations)
		{
		try
		{
			std::ofstream file(path);
			
			if (file.is_open()) {
				//clear the file
				file.clear();
				
				for (size_t i = 0; i < mRecordingPathPositions->size(); ++i) {
					file << mRecordingPathPositions->at(i).x << " " << mRecordingPathPositions->at(i).y << " " << mRecordingPathPositions->at(i).z << " ";
					file << mRecordingPathRotations->at(i).x << " " << mRecordingPathRotations->at(i).y << " " << mRecordingPathRotations->at(i).z << std::endl;
				}
				file.close();
			}
		}
		catch (std::exception& e)
		{
			std::cerr << "Error saving camera path to disk: " << e.what() << std::endl;
		}
		//deallocating the memory
		mRecordingPathPositions.reset();
		mRecordingPathRotations.reset();
	}

	void save_to_disk(std::string path)
	{
		std::thread t(&camera_path_recorder::_save_to_disk, this, path, std::move(mRecordingPathPositions), std::move(mRecordingPathRotations));
		t.detach();
	}

private:
	avk::quake_camera*  mCam;
	float mStartTime;
	float mLastRecordedTime;
	float mRecordingDensity;
	bool mRecording = false;
	std::unique_ptr<std::vector<glm::vec3>> mRecordingPathPositions;
	std::unique_ptr<std::vector<glm::vec3>> mRecordingPathRotations;

};
