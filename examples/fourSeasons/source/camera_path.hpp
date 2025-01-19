#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <glm/vec3.hpp>
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
        , mStartTime{ avk::time().time_since_start() }
        , mRecordingDensity(0.5f) // Default recording density is 0.05 seconds
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

        // Create bezier curves for every 5 control points, reusing the last control point
        for (size_t i = 0; i < positions.size(); i += 4) {
            std::vector<glm::vec3> posChunk(positions.begin() + i, positions.begin() + std::min(i + 5, positions.size()));
            mPathPositions.push_back(std::make_unique<avk::bezier_curve>(posChunk));
        }

        for (size_t i = 0; i < rotations.size(); i += 4) {
            std::vector<glm::vec3> rotChunk(rotations.begin() + i, rotations.begin() + std::min(i + 5, rotations.size()));
            mRotationPath.push_back(std::make_unique<avk::bezier_curve>(rotChunk));
        }
    }

    void update()
    {
        // Use the RecordingDensity to move the camera along the path
        // Assume each control point is recorded at exactly RecordingDensity seconds

        auto t = (avk::time().time_since_start() - mStartTime);
        auto numberOfControlPoints = mPathPositions.size() * 4 + 1;
        auto total_time_span = mRecordingDensity * numberOfControlPoints;
        auto percentage = t / total_time_span * mSpeed;
        if (percentage > 1.0f) {
            // restart the path
            mStartTime = avk::time().time_since_start();
            percentage = 0.0f;
        }

        // Determine which bezier curve to use
        size_t curveIndex = static_cast<size_t>(percentage * mPathPositions.size());
        curveIndex = std::min(curveIndex, mPathPositions.size() - 1);

        // Calculate the local percentage within the selected bezier curve
        float localPercentage = (percentage * mPathPositions.size()) - curveIndex;

        // Update the camera position and rotation
        mCam->set_translation(mPathPositions[curveIndex]->value_at(localPercentage));
        mCam->look_along(mRotationPath[curveIndex]->value_at(localPercentage));
    }

private:
    avk::quake_camera* mCam;
    float mSpeed;
    float mStartTime;
    float mRecordingDensity;
    std::vector<std::unique_ptr<avk::bezier_curve>> mPathPositions;
    std::vector<std::unique_ptr<avk::bezier_curve>> mRotationPath;
};