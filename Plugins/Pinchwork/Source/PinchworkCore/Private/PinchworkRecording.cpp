// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "Pinchwork/PinchworkRecording.h"
#include <sstream>
#include <iomanip>

namespace Pinchwork
{
	static const char* kHeader = "PINCHWORK_REC v1";

	std::string Serialize(const FRecording& Rec)
	{
		std::ostringstream Out;
		Out << kHeader << "\n";
		Out << "# F <time> " << KKeypointCount << "x(x y z)  V " << KKeypointCount << " validity bits\n";
		Out << std::setprecision(7);
		for (const FRecordedFrame& Frame : Rec.Frames)
		{
			Out << "F " << Frame.TimeSec;
			for (int i = 0; i < KKeypointCount; ++i)
			{
				const FVec3& J = Frame.Pose.Joints[i];
				Out << ' ' << J.X << ' ' << J.Y << ' ' << J.Z;
			}
			Out << " V ";
			for (int i = 0; i < KKeypointCount; ++i)
			{
				Out << (Frame.Pose.bValid[i] ? '1' : '0');
			}
			Out << "\n";
		}
		return Out.str();
	}

	bool Deserialize(const std::string& Text, FRecording& OutRec)
	{
		OutRec.Frames.clear();
		std::istringstream In(Text);
		std::string Line;
		bool bHeaderSeen = false;

		while (std::getline(In, Line))
		{
			// Trim trailing CR (tolerate CRLF fixtures) and skip blanks/comments.
			if (!Line.empty() && Line.back() == '\r') { Line.pop_back(); }
			if (Line.empty() || Line[0] == '#') { continue; }

			if (!bHeaderSeen)
			{
				if (Line.rfind("PINCHWORK_REC", 0) != 0) { return false; } // bad/missing header
				bHeaderSeen = true;
				continue;
			}

			std::istringstream LineIn(Line);
			std::string Tag;
			LineIn >> Tag;
			if (Tag != "F") { continue; }

			FRecordedFrame Frame;
			if (!(LineIn >> Frame.TimeSec)) { return false; }
			for (int i = 0; i < KKeypointCount; ++i)
			{
				if (!(LineIn >> Frame.Pose.Joints[i].X >> Frame.Pose.Joints[i].Y >> Frame.Pose.Joints[i].Z))
				{
					return false;
				}
			}
			// Optional "V <bits>" validity. Absent ⇒ all joints valid.
			std::string VTag;
			if (LineIn >> VTag && VTag == "V")
			{
				std::string Bits;
				LineIn >> Bits;
				for (int i = 0; i < KKeypointCount; ++i)
				{
					Frame.Pose.bValid[i] = (i < (int)Bits.size()) ? (Bits[i] == '1') : true;
				}
			}
			else
			{
				for (int i = 0; i < KKeypointCount; ++i) { Frame.Pose.bValid[i] = true; }
			}
			OutRec.Frames.push_back(Frame);
		}
		return bHeaderSeen;
	}

	std::vector<EGesture> ReplayRecognized(
		const FRecording& Rec, const FGestureConfig& Config, const FCalibration& Calib, float StabilityWindowSec)
	{
		std::vector<EGesture> Timeline;
		Timeline.reserve(Rec.Frames.size());

		FGestureStabilizer Stab;
		Stab.StabilityWindowSec = StabilityWindowSec;

		float PrevTime = Rec.Frames.empty() ? 0.0f : Rec.Frames[0].TimeSec;
		for (const FRecordedFrame& Frame : Rec.Frames)
		{
			const float Dt = (Frame.TimeSec - PrevTime) > 0.0f ? (Frame.TimeSec - PrevTime) : 0.0f;
			PrevTime = Frame.TimeSec;
			const EGesture Detected = RecognizeGesture(Frame.Pose, Config, Calib).Gesture;
			Timeline.push_back(Stab.Update(Detected, Dt));
		}
		return Timeline;
	}
}
