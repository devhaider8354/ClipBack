#include "audio/AudioMixer.h"
#include "logging/Logger.h"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace clipback {
namespace { constexpr size_t kBlock = 1024, kMaxQueue = kAudioSampleRate / 2; }
AudioMixer::~AudioMixer() { Stop(); }
void AudioMixer::SourceQueue::Push(const float* d, size_t f, size_t max) { std::lock_guard<std::mutex> l(mutex); samples.insert(samples.end(), d, d + f * 2); if (samples.size() > max * 2) samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - max * 2)); }
size_t AudioMixer::SourceQueue::Pop(float* out, size_t f) { std::lock_guard<std::mutex> l(mutex); const size_t n = std::min(samples.size() / 2, f); std::copy_n(samples.begin(), n * 2, out); samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(n * 2)); return n; }
bool AudioMixer::InitializeTrack(AudioTrack t, UINT br, PacketCallback cb, std::wstring& e) { const size_t i = static_cast<size_t>(t); encoders_[i] = std::make_unique<AacEncoder>(); if (!encoders_[i]->Initialize(br, [t, cb = std::move(cb)](EncodedPacket&& p) mutable { cb(t, std::move(p)); }, e)) return false; enabledTracks_[i] = true; return true; }
bool AudioMixer::Start(const AudioConfig& c, int64_t start, PacketCallback cb, std::wstring& e) {
 Stop(); sessionStart_ = start; producedFrames_ = 0; micGain_ = c.microphoneGain; stop_ = false; enabledTracks_.fill(false);
 if ((c.mode == AudioMode::Mixed || c.mode == AudioMode::Three) && !InitializeTrack(AudioTrack::Mix, c.aacBitrateKbps, cb, e)) { Stop(); return false; }
 if (c.mode == AudioMode::Separate || c.mode == AudioMode::Three || c.mode == AudioMode::PcOnly) {
  if (!InitializeTrack(AudioTrack::Game, c.aacBitrateKbps, cb, e)) { Stop(); return false; }
  if ((c.mode == AudioMode::Separate || c.mode == AudioMode::Three) && !InitializeTrack(AudioTrack::Microphone, c.aacBitrateKbps, cb, e)) { Stop(); return false; }
 }
 std::wstring ge, me; gameActive_ = game_.Start(c.outputDeviceId, true, [this](const float* s,size_t n){gameQueue_.Push(s,n,kMaxQueue);},ge); if (!gameActive_) LOG_WARN(L"Game audio unavailable; encoding silence: %s",ge.c_str());
 micActive_ = c.microphoneEnabled && mic_.Start(c.microphoneDeviceId,false,[this](const float* s,size_t n){micQueue_.Push(s,n,kMaxQueue);},me); if (c.microphoneEnabled && !micActive_) LOG_WARN(L"Microphone unavailable; encoding silence: %s",me.c_str());
 thread_ = std::thread(&AudioMixer::ThreadMain,this); return true;
}
void AudioMixer::Stop() { {std::lock_guard<std::mutex> l(mutex_); stop_=true;} cv_.notify_all(); if(thread_.joinable()) thread_.join(); game_.Stop(); mic_.Stop(); for(auto& x:encoders_){if(x)x->Shutdown();x.reset();} enabledTracks_.fill(false); gameActive_=micActive_=false; {std::lock_guard<std::mutex>l(gameQueue_.mutex);gameQueue_.samples.clear();}{std::lock_guard<std::mutex>l(micQueue_.mutex);micQueue_.samples.clear();} }
ComPtr<IMFMediaType> AudioMixer::OutputType(AudioTrack t) const { const auto& e=encoders_[static_cast<size_t>(t)]; return e?e->OutputType():nullptr; }
bool AudioMixer::HasTrack(AudioTrack t) const { return enabledTracks_[static_cast<size_t>(t)]; }
bool AudioMixer::HasFailed(std::wstring& e) const { if(gameActive_&&game_.HasFailed(e))return true; return micActive_&&mic_.HasFailed(e); }
void AudioMixer::ThreadMain() {
 ComInitializer com; std::vector<float> g(kBlock*2),m(kBlock*2),x(kBlock*2); std::vector<int16_t> gp(kBlock*2),mp(kBlock*2),xp(kBlock*2); std::unique_lock<std::mutex> l(mutex_);
 while(!stop_){cv_.wait_for(l,std::chrono::milliseconds(10),[&]{return stop_;});if(stop_)break;l.unlock();const int64_t target=(Now100ns()-sessionStart_)*kAudioSampleRate/kTicksPerSecond;int blocks=0;
  while(producedFrames_ + static_cast<int64_t>(kBlock) <= target && blocks++ < 8){const size_t gn=gameActive_?gameQueue_.Pop(g.data(),kBlock):0;std::fill(g.begin()+gn*2,g.end(),0.0f);const size_t mn=micActive_?micQueue_.Pop(m.data(),kBlock):0;std::fill(m.begin()+mn*2,m.end(),0.0f);for(size_t i=0;i<kBlock*2;++i){x[i]=std::clamp(g[i]+m[i]*micGain_,-1.f,1.f);gp[i]=(int16_t)std::lrint(std::clamp(g[i],-1.f,1.f)*32767);mp[i]=(int16_t)std::lrint(std::clamp(m[i]*micGain_,-1.f,1.f)*32767);xp[i]=(int16_t)std::lrint(x[i]*32767);}const int64_t pts=producedFrames_*kTicksPerSecond/kAudioSampleRate;if(enabledTracks_[0])encoders_[0]->Encode(xp.data(),kBlock,pts);if(enabledTracks_[1])encoders_[1]->Encode(gp.data(),kBlock,pts);if(enabledTracks_[2])encoders_[2]->Encode(mp.data(),kBlock,pts);producedFrames_+=kBlock;}
  if(target-producedFrames_>kAudioSampleRate*2) producedFrames_=target-target%kBlock;l.lock(); }
}
} // namespace clipback
