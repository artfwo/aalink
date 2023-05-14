#include <pybind11/chrono.h>
#include <pybind11/pybind11.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <thread>

#include <ableton/Link.hpp>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

const double QUANTUM = 1.0;

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

namespace py = pybind11;

struct SchedulerSyncEvent {
    py::object future;

    double beat;
    double offset;
    double origin;
    double link_beat;
};

struct Scheduler {
    Scheduler(ableton::Link& link, py::object loop) : m_link(link), m_loop(loop) {
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
            auto link_beat = link_state.beatAtTime(link_time, QUANTUM);

            m_link_beat = link_beat;
            m_link_time = link_time.count() / 1e6;

            m_events_mutex.lock();

            for (auto it = m_events.begin(); it != m_events.end();) {
                if (link_beat > it->link_beat) {
                    py::gil_scoped_acquire acquire;

                    auto future_done = it->future.attr("done")().cast<bool>();

                    if (!future_done) {
                        auto loop_call_soon_threadsafe = m_loop.attr("call_soon_threadsafe");
                        auto future_set_result = it->future.attr("set_result");
                        loop_call_soon_threadsafe(future_set_result, it->link_beat);
                    }

                    py::gil_scoped_release release;

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

    void schedule_sync(py::object future, double beat, double offset, double origin) {
        // prevent occasional GIL deadlocks when calling link.sync()
        py::gil_scoped_release release;

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

    ableton::Link& m_link;
    py::object m_loop;
};

struct Link : ableton::Link {
    Link(double bpm, py::object loop)
        : ableton::Link(bpm), m_loop(loop), m_scheduler(*this, m_loop) {}

    py::object sync(double beat, double offset, double origin) {
        auto future = m_loop.attr("create_future")();
        m_scheduler.schedule_sync(future, beat, offset, origin);
        return future;
    }

    py::object m_loop;
    Scheduler m_scheduler;
};

PYBIND11_MODULE(aalink, m) {
    py::class_<ableton::Link::Clock>(m, "Clock")
        .def("time", &ableton::Link::Clock::micros);

    py::class_<ableton::Link::SessionState>(m, "SessionState")
        .def("tempo", &ableton::Link::SessionState::tempo)
        .def("set_tempo", &ableton::Link::SessionState::setTempo)
        .def("beat_at_time", &ableton::Link::SessionState::beatAtTime)
        .def("phase_at_time", &ableton::Link::SessionState::phaseAtTime)
        .def("time_at_beat", &ableton::Link::SessionState::timeAtBeat)
        .def("request_beat_at_time", &ableton::Link::SessionState::requestBeatAtTime)
        .def("force_beat_at_time", &ableton::Link::SessionState::forceBeatAtTime)
        .def("set_is_playing", &ableton::Link::SessionState::setIsPlaying)
        .def("is_playing", &ableton::Link::SessionState::isPlaying)
        .def("time_for_is_playing", &ableton::Link::SessionState::timeForIsPlaying)
        .def("request_beat_at_start_playing_time", &ableton::Link::SessionState::requestBeatAtStartPlayingTime)
        .def("set_is_playing_and_request_beat_at_time", &ableton::Link::SessionState::setIsPlayingAndRequestBeatAtTime);

    py::class_<Link>(m, "Link")
        .def(py::init<double, py::object>())
        .def("enable", &Link::enable)
        .def("clock", &Link::clock)
        .def("capture_app_session_state", &Link::captureAppSessionState)
        .def("commit_app_session_state", &Link::commitAppSessionState)
        .def("sync", &Link::sync, py::arg("beat"), py::arg("offset") = 0, py::arg("origin") = 0);

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
