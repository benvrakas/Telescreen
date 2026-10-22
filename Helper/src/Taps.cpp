#include "Taps.h"

#include <map>
#include <mutex>

namespace
{
	template <typename TTap>
	struct FRegistry
	{
		std::mutex Mutex;
		std::map<int, std::pair<uint32_t, TTap>> Taps;
		int NextId = 1;

		int Add(uint32_t Source, TTap Tap)
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			const int Id = NextId++;
			Taps[Id] = {Source, std::move(Tap)};
			return Id;
		}

		void Remove(int Id)
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Taps.erase(Id);
		}

		template <typename... TArgs>
		void Publish(uint32_t Source, TArgs... Args)
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			for (auto& Pair : Taps)
			{
				if (Pair.second.first == Source) Pair.second.second(Args...);
			}
		}
	};

	FRegistry<FrameTaps::FTap> GFrames;
	FRegistry<AudioTaps::FTap> GAudio;
}

int FrameTaps::Add(uint32_t SourceSession, FTap Tap) { return GFrames.Add(SourceSession, std::move(Tap)); }
void FrameTaps::Remove(int TapId) { GFrames.Remove(TapId); }
void FrameTaps::Publish(uint32_t SourceSession, const uint8_t* Bgra, int Width, int Height) { GFrames.Publish(SourceSession, Bgra, Width, Height); }

int AudioTaps::Add(uint32_t SourceSession, FTap Tap) { return GAudio.Add(SourceSession, std::move(Tap)); }
void AudioTaps::Remove(int TapId) { GAudio.Remove(TapId); }
void AudioTaps::Publish(uint32_t SourceSession, const float* Interleaved, int Frames) { GAudio.Publish(SourceSession, Interleaved, Frames); }
