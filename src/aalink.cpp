#include <nanobind/stl/chrono.h>
#include <nanobind/nanobind.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <thread>

#include <ableton/Link.hpp>

namespace nb = nanobind;

struct SchedulerSyncEvent {
    nb::object future;

    double beat;
    double offset;
    double origin;
    double link_beat;
};

static double next_link_beat(double current_beat, double sync_beat, double offset, double origin) {
    double next_beat;

    next_beat = floor((current_beat - origin) / sync_beat) + 1.0;
    next_beat = next_beat * sync_beat + origin;
    next_beat = next_beat + offset;

    while (next_beat <= current_beat) {
        next_beat += sync_beat;
    }

    return std::max(next_beat, 0.0);
}

static void set_future_result(nb::object future, double link_beat) {
    nb::gil_scoped_acquire acquire;

    bool done = nb::cast<bool>(future.attr("done")());

    if (!done) {
        auto set_result = future.attr("set_result");
        set_result(link_beat);
    }
}

struct Scheduler {
    Scheduler(ableton::Link& link, nb::object loop) : m_link(link), m_loop(loop) {
        start();
    }

    ~Scheduler() { stop(); }

    void start() {
        m_stop_thread = false;
        m_thread = std::thread(&Scheduler::run, this);
    }

    void stop() {
        if (m_thread.joinable()) {
            m_stop_thread = true;
            m_thread.join();
        }
    }

    void run() {
        using namespace std::chrono_literals;

        while (true) {
            auto link_state = m_link.captureAppSessionState();

            auto link_time = m_link.clock().micros();
            auto link_beat = link_state.beatAtTime(link_time, m_link_quantum);

            m_link_beat = link_beat;
            m_link_time = link_time.count() / 1e6;

            m_events_mutex.lock();

            for (auto it = m_events.begin(); it != m_events.end();) {
                if (link_beat > it->link_beat) {
                    nb::gil_scoped_acquire acquire;

                    auto loop_call_soon_threadsafe = m_loop.attr("call_soon_threadsafe");
                    loop_call_soon_threadsafe(nb::cpp_function(&set_future_result), it->future, it->link_beat);

                    it = m_events.erase(it);
                } else {
                    ++it;
                }
            }

            m_events_mutex.unlock();

            if (m_stop_thread) {
                break;
            }

            std::this_thread::sleep_for(1ms);
        }
    }

    void schedule_sync(nb::object future, double beat, double offset, double origin) {
        // prevent occasional GIL deadlocks when calling link.sync()
        nb::gil_scoped_release release;

        SchedulerSyncEvent event = {
            .future = future,
            .beat = beat,
            .offset = offset,
            .origin = origin,
            .link_beat = next_link_beat(m_link_beat, beat, offset, origin),
        };

        m_events_mutex.lock();
        m_events.push_back(std::move(event));
        m_events_mutex.unlock();
    }

    void reset_sync_events() {
        m_events_mutex.lock();

        for (auto& event : m_events) {
            event.link_beat = 0;
        }

        m_events_mutex.unlock();
    }

    void reschedule_sync_events(double link_beat) {
        m_events_mutex.lock();

        for (auto& event : m_events) {
            event.link_beat = next_link_beat(link_beat, event.beat, event.offset, event.origin);
        }

        m_events_mutex.unlock();
    }

    std::thread m_thread;
    std::atomic<bool> m_stop_thread;
    std::mutex m_events_mutex;
    std::list<SchedulerSyncEvent> m_events;

    std::atomic<double> m_link_beat{0};
    std::atomic<double> m_link_time{0};
    std::atomic<double> m_link_quantum{1};

    ableton::Link& m_link;
    nb::object m_loop;
};

struct Link {
    Link(double bpm, nb::object loop)
        : m_link(bpm), m_loop(loop), m_scheduler(m_link, m_loop) {}

    std::size_t num_peers() {
        return m_link.numPeers();
    }

    double beat() {
        auto link_state = m_link.captureAppSessionState();
        return link_state.beatAtTime(m_link.clock().micros(), m_scheduler.m_link_quantum);
    }

    double phase() {
        auto link_state = m_link.captureAppSessionState();
        return link_state.phaseAtTime(m_link.clock().micros(), m_scheduler.m_link_quantum);
    }

    std::chrono::microseconds time() {
        return m_link.clock().micros();
    }

    double quantum() {
        return m_scheduler.m_link_quantum;
    }

    void set_quantum(double quantum) {
        m_scheduler.m_link_quantum = quantum;
    }

    bool enabled() {
        return m_link.isEnabled();
    }

    void set_enabled(bool enabled) {
        m_link.enable(enabled);
    }

    bool start_stop_sync_enabled() {
        return m_link.isStartStopSyncEnabled();
    }

    void set_start_stop_sync_enabled(bool enabled) {
        m_link.enableStartStopSync(enabled);
    }

    double tempo() {
        auto link_state = m_link.captureAppSessionState();
        return link_state.tempo();
    }

    void set_tempo(double tempo) {
        auto link_state = m_link.captureAppSessionState();
        link_state.setTempo(tempo, m_link.clock().micros());
        m_link.commitAppSessionState(link_state);
    }

    bool playing() {
        auto link_state = m_link.captureAppSessionState();
        return link_state.isPlaying();
    }

    void set_playing(bool playing) {
        auto link_state = m_link.captureAppSessionState();
        link_state.setIsPlaying(playing, m_link.clock().micros());
        m_link.commitAppSessionState(link_state);
    }

    void request_beat(double beat) {
        auto link_state = m_link.captureAppSessionState();
        link_state.requestBeatAtTime(beat, m_link.clock().micros(), m_scheduler.m_link_quantum);
        m_link.commitAppSessionState(link_state);
    }

    void force_beat(double beat) {
        auto link_state = m_link.captureAppSessionState();
        link_state.forceBeatAtTime(beat, m_link.clock().micros(), m_scheduler.m_link_quantum);
        m_link.commitAppSessionState(link_state);
    }

    void request_beat_at_start_playing_time(double beat) {
        auto link_state = m_link.captureAppSessionState();
        link_state.requestBeatAtStartPlayingTime(beat, m_scheduler.m_link_quantum);
        m_link.commitAppSessionState(link_state);
    }

    void set_is_playing_and_request_beat_at_time(bool playing, std::chrono::microseconds time, double beat) {
        auto link_state = m_link.captureAppSessionState();
        link_state.setIsPlayingAndRequestBeatAtTime(playing, time, beat, m_scheduler.m_link_quantum);
        m_link.commitAppSessionState(link_state);
    }

    nb::object sync(double beat, double offset, double origin) {
        auto future = m_loop.attr("create_future")();
        m_scheduler.schedule_sync(future, beat, offset, origin);
        return future;
    }

    ableton::Link m_link;
    nb::object m_loop;
    Scheduler m_scheduler;
};

NB_MODULE(aalink, m) {
    nb::class_<Link>(m, "Link")
        .def(nb::init<double, nb::object>(), nb::arg("bpm"), nb::arg("loop"))
        .def_prop_ro("num_peers", &Link::num_peers)
        .def_prop_ro("beat", &Link::beat)
        .def_prop_ro("phase", &Link::phase)
        .def_prop_ro("time", &Link::time)
        .def_prop_rw("quantum", &Link::quantum, &Link::set_quantum)
        .def_prop_rw("enabled", &Link::enabled, &Link::set_enabled)
        .def_prop_rw("start_stop_sync_enabled", &Link::start_stop_sync_enabled, &Link::set_start_stop_sync_enabled)
        .def_prop_rw("tempo", &Link::tempo, &Link::set_tempo)
        .def_prop_rw("playing", &Link::playing, &Link::set_playing)
        .def("request_beat", &Link::request_beat)
        .def("force_beat", &Link::force_beat)
        .def("request_beat_at_start_playing_time", &Link::request_beat_at_start_playing_time)
        .def("set_is_playing_and_request_beat_at_time", &Link::set_is_playing_and_request_beat_at_time)
        .def("sync", &Link::sync, nb::arg("beat"), nb::arg("offset") = 0, nb::arg("origin") = 0);
}
