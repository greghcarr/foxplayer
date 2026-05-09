// Windows implementation of NowPlayingBridge. Hooks the JUCE side up to the
// Windows SystemMediaTransportControls (SMTC), which is the API that drives
// the small media flyout above the volume slider, the lock-screen Now
// Playing card, and Bluetooth / hardware media-key control. SMTC is window-
// bound on the desktop (a process can host one set of controls per HWND),
// so we lazily resolve the main MainWindow HWND on the first call once
// MainWindow has had a chance to construct its peer.
//
// All WinRT objects are created in a helper struct kept behind a void*
// (impl_) so the public header stays platform-free.

#include "NowPlayingBridge.h"

#include <JuceHeader.h>

#include <windows.h>
#include <systemmediatransportcontrolsinterop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "runtimeobject.lib")

namespace winrt_media = winrt::Windows::Media;

namespace Stylus
{

namespace
{
// Walks every JUCE top-level window and returns the first one whose peer has
// a native HWND. SMTC is bound per-HWND, so we want the MainWindow rather
// than transient pop-ups; the first top-level window is normally MainWindow.
HWND findHostHwnd()
{
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        if (auto* w = juce::TopLevelWindow::getTopLevelWindow(i))
            if (auto* peer = w->getPeer())
                if (auto* h = peer->getNativeHandle())
                    return (HWND) h;
    return nullptr;
}

// One-shot apartment init for the calling thread. SMTC is happy with either
// STA or MTA; we use multi_threaded so background callbacks (button events)
// don't have to marshal through the UI dispatcher.
void ensureApartment()
{
    static thread_local bool done = false;
    if (done) return;
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    catch (winrt::hresult_error const&) { /* already initialised */ }
    done = true;
}
} // namespace

struct SmtcState
{
    HWND                                         hwnd        { nullptr };
    winrt_media::SystemMediaTransportControls    smtc        { nullptr };
    winrt_media::SystemMediaTransportControlsDisplayUpdater updater { nullptr };
    winrt::event_token                           buttonToken {};
    bool                                         hasTrack    { false };
    double                                       duration    { 0.0 };

    bool tryInit(NowPlayingBridge* owner)
    {
        if (smtc) return true;

        hwnd = findHostHwnd();
        if (! hwnd) return false;

        ensureApartment();

        // ISystemMediaTransportControlsInterop is the bridge between desktop
        // HWND-based apps and the otherwise UWP-shaped SMTC API.
        winrt::com_ptr<ISystemMediaTransportControlsInterop> interop;
        try
        {
            interop = winrt::get_activation_factory<
                          winrt_media::SystemMediaTransportControls,
                          ISystemMediaTransportControlsInterop>();
        }
        catch (winrt::hresult_error const&) { return false; }

        winrt_media::SystemMediaTransportControls created { nullptr };
        const HRESULT hr = interop->GetForWindow(
            hwnd,
            winrt::guid_of<winrt_media::SystemMediaTransportControls>(),
            winrt::put_abi(created));
        if (FAILED(hr) || ! created) return false;

        smtc = created;
        smtc.IsEnabled(true);
        smtc.IsPlayEnabled(true);
        smtc.IsPauseEnabled(true);
        smtc.IsNextEnabled(true);
        smtc.IsPreviousEnabled(true);
        smtc.IsStopEnabled(false);
        smtc.PlaybackStatus(winrt_media::MediaPlaybackStatus::Closed);

        updater = smtc.DisplayUpdater();
        updater.Type(winrt_media::MediaPlaybackType::Music);

        // Button events arrive on a Windows thread pool worker. Marshal each
        // call onto JUCE's message thread before invoking the std::function
        // callback so the rest of the app can stay single-threaded.
        buttonToken = smtc.ButtonPressed(
            [owner](winrt_media::SystemMediaTransportControls const&,
                    winrt_media::SystemMediaTransportControlsButtonPressedEventArgs const& args)
            {
                const auto button = args.Button();
                juce::MessageManager::callAsync([owner, button]
                {
                    if (! owner) return;
                    using B = winrt_media::SystemMediaTransportControlsButton;
                    if (button == B::Play || button == B::Pause)
                    {
                        if (owner->onPlayPause) owner->onPlayPause();
                    }
                    else if (button == B::Next)
                    {
                        if (owner->onNext) owner->onNext();
                    }
                    else if (button == B::Previous)
                    {
                        if (owner->onPrevious) owner->onPrevious();
                    }
                });
            });

        return true;
    }

    void shutdown()
    {
        if (smtc)
        {
            try { smtc.ButtonPressed(buttonToken); } catch (...) {}
            try
            {
                smtc.IsEnabled(false);
                smtc.PlaybackStatus(winrt_media::MediaPlaybackStatus::Closed);
            }
            catch (...) {}
        }
        smtc    = nullptr;
        updater = nullptr;
    }
};

NowPlayingBridge::NowPlayingBridge()
    : impl_(new SmtcState)
{}

NowPlayingBridge::~NowPlayingBridge()
{
    if (auto* st = static_cast<SmtcState*>(impl_))
    {
        st->shutdown();
        delete st;
        impl_ = nullptr;
    }
}

void NowPlayingBridge::setTrackInfo(const std::string& title,
                                    const std::string& artist,
                                    double             durationSeconds)
{
    auto* st = static_cast<SmtcState*>(impl_);
    if (! st || ! st->tryInit(this)) return;

    try
    {
        st->duration = durationSeconds;
        st->hasTrack = true;

        const auto hsTitle  = winrt::to_hstring(title);
        const auto hsArtist = winrt::to_hstring(artist);

        auto music = st->updater.MusicProperties();
        music.Title(hsTitle);
        music.Artist(hsArtist);
        st->updater.Update();

        // Initial timeline: position 0, end = duration. SMTC wants positive
        // EndTime to render the scrubber on the lock screen / flyout.
        winrt_media::SystemMediaTransportControlsTimelineProperties timeline;
        timeline.StartTime(std::chrono::seconds(0));
        timeline.MinSeekTime(std::chrono::seconds(0));
        timeline.Position(std::chrono::seconds(0));
        const auto endTicks = (long long) (durationSeconds * 1.0e7);
        timeline.EndTime(winrt::Windows::Foundation::TimeSpan { endTicks });
        timeline.MaxSeekTime(winrt::Windows::Foundation::TimeSpan { endTicks });
        st->smtc.UpdateTimelineProperties(timeline);
    }
    catch (winrt::hresult_error const&) {}
}

void NowPlayingBridge::setPlaybackState(bool isPlaying, double posSeconds)
{
    auto* st = static_cast<SmtcState*>(impl_);
    if (! st || ! st->tryInit(this)) return;

    try
    {
        st->smtc.PlaybackStatus(isPlaying
            ? winrt_media::MediaPlaybackStatus::Playing
            : winrt_media::MediaPlaybackStatus::Paused);

        if (st->hasTrack)
        {
            winrt_media::SystemMediaTransportControlsTimelineProperties timeline;
            const auto posTicks = (long long) (posSeconds         * 1.0e7);
            const auto endTicks = (long long) (st->duration       * 1.0e7);
            timeline.StartTime(std::chrono::seconds(0));
            timeline.MinSeekTime(std::chrono::seconds(0));
            timeline.Position(winrt::Windows::Foundation::TimeSpan { posTicks });
            timeline.EndTime (winrt::Windows::Foundation::TimeSpan { endTicks });
            timeline.MaxSeekTime(winrt::Windows::Foundation::TimeSpan { endTicks });
            st->smtc.UpdateTimelineProperties(timeline);
        }
    }
    catch (winrt::hresult_error const&) {}
}

void NowPlayingBridge::clearNowPlaying()
{
    auto* st = static_cast<SmtcState*>(impl_);
    if (! st || ! st->smtc) return;

    try
    {
        st->hasTrack = false;
        st->duration = 0.0;
        st->smtc.PlaybackStatus(winrt_media::MediaPlaybackStatus::Stopped);
        st->updater.ClearAll();
        st->updater.Update();
    }
    catch (winrt::hresult_error const&) {}
}

} // namespace Stylus
