#pragma once

#include "bezier_curve.hpp"
#include "invokee.hpp"
#include "quake_camera.hpp"
#include "timer_interface.hpp"

class camera_path
{
public:
	camera_path(avk::quake_camera& cam, std::string filepath)
		: mCam{ &cam } // Target camera
		, mSpeed{ 1.0f } // How fast does it move
		, mStartTime{avk::time().time_since_start()}
		, mRecordingDensity(0.5) // Default recording density is 0.5 seconds
	{
		std::vector<glm::vec3> positions = {};
		std::vector<glm::vec3> rotations = {};
		
		// Load the path from the file
		std::ifstream file(filepath);
		if (file.is_open()) {
			std::string line;
			while (std::getline(file, line)) {
				std::istringstream iss(line);
				glm::vec3 pos;
				glm::vec3 rot;
				iss >> pos.x >> pos.y >> pos.z >> rot.x >> rot.y >> rot.z;
				positions.push_back(pos);
				rotations.push_back(rot);
			}
			file.close();
		}
		else {
			throw avk::runtime_error("Could not open file " + filepath);
		}

		mPathPositions = std::make_unique<avk::bezier_curve>(positions);
		mRotationPath = std::make_unique<avk::bezier_curve>(rotations);
	}
	
	void update()
	{
		// Use the RecoringDensity to move the camera along the path
		// Assume each control point is recorded at exactly RecordingDensity seconds
		
		auto t = (avk::time().time_since_start() - mStartTime);
		auto numberOfControlPoints = mPathPositions->num_control_points();
		auto total_time_span = mRecordingDensity * numberOfControlPoints;
		auto percentage = t / total_time_span * mSpeed;
		if (percentage > 1.0f) {
			// restart the path
			mStartTime = avk::time().time_since_start();
			percentage = 0.0f;
		}
		
		// Update the camera position and rotation
		mCam->set_translation(mPathPositions->value_at(percentage));
		mCam->look_along(mRotationPath->value_at(percentage));
	}

private:
	avk::quake_camera* mCam;
	float mSpeed;
	float mStartTime;
	float mRecordingDensity;
	std::unique_ptr<avk::cp_interpolation> mPathPositions;
	std::unique_ptr<avk::cp_interpolation> mRotationPath;

};
