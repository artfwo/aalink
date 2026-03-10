#include <pybind11/chrono.h>
#include <pybind11/pybind11.h>
#include <pybind11/warnings.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <memory>
#include <thread>

#include <ableton/Link.hpp>

namespace py = pybind11;

struct SchedulerSyncEvent {
    py::object future;

    double beat;
    double offset;
    double origin;
    double link_beat;
};

static double next_link_beat(double current_beat, double sync_beat, double offset, double origin) {
    double next_beat;
    double i;

    // return current_beat if evenly divisible by sync_beat
    if (modf(current_beat / sync_beat, &i) == 0) {
        return current_beat;
    }

    next_beat = floor((current_beat - origin) / sync_beat) + 1.0;
    next_beat = next_beat * sync_beat + origin;
    next_beat = next_beat + offset;

    while (next_beat <= current_beat) {
        next_beat += sync_beat;
    }

    return std::max(next_beat, 0.0);
}

static void set_future_result(py::object future, double link_beat) {
    py::gil_scoped_acquire acquire;

    bool done = py::cast<bool>(future.attr("done")());

    if (!done) {
        auto set_result = future.attr("set_result");
        set_result(link_beat);
    }
}

struct Scheduler {
    Scheduler(ableton::Link& link, py::object loop) : m_link(link), m_loop(loop) {
        m_thread = std::thread(&Scheduler::run, this);
    }

    ~Scheduler() {
        m_stop_thread.store(true, std::memory_order_relaxed);

        // workaround for a rare deadlock during interpreter shutdown: the scheduler
        // thread may be blocked on GIL while jthread's destructor waits in join()
        #if PY_VERSION_HEX < 0x30d0000
        if (_Py_IsFinalizing()) {
        #else
        if (Py_IsFinalizing()) {
        #endif
            m_thread.detach();
        } else {
            m_thread.join();
        }
    }

    void run() {
        using namespace std::chrono_literals;

        while (!m_stop_thread.load(std::memory_order_relaxed)) {
            auto link_state = m_link.captureAppSessionState();

            auto link_time = m_link.clock().micros();
            auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
            auto link_beat = link_state.beatAtTime(link_time, link_quantum);

            m_link_beat.store(link_beat, std::memory_order_relaxed);
            m_link_time.store(link_time.count() / 1e6, std::memory_order_relaxed);

            m_events_mutex.lock();

            for (auto it = m_events.begin(); it != m_events.end();) {
                if (link_beat > it->link_beat) {
                    py::gil_scoped_acquire acquire;

                    bool loop_is_running = py::cast<bool>(m_loop.attr("is_running")());

                    if (loop_is_running) {
                        auto loop_call_soon_threadsafe = m_loop.attr("call_soon_threadsafe");
                        loop_call_soon_threadsafe(py::cpp_function(&set_future_result), it->future, it->link_beat);
                    }

                    it = m_events.erase(it);
                } else {
                    ++it;
                }
            }

            m_events_mutex.unlock();

            std::this_thread::sleep_for(1ms);
        }
    }

    void schedule_sync(py::object future, double beat, double offset, double origin) {
        auto link_beat = m_link_beat.load(std::memory_order_relaxed);

        SchedulerSyncEvent event = {
            .future = future,
            .beat = beat,
            .offset = offset,
            .origin = origin,
            .link_beat = next_link_beat(link_beat, beat, offset, origin),
        };

        // prevent occasional deadlocks when the scheduler thread locks
        // m_events_mutex first and then acquires the GIL to dispatch callbacks
        py::gil_scoped_release release;

        m_events_mutex.lock();
        m_events.push_back(std::move(event));
        m_events_mutex.unlock();
    }

    void reschedule_sync_events(double link_beat) {
        // prevent occasional deadlocks when the scheduler thread locks
        // m_events_mutex first and then acquires the GIL to dispatch callbacks
        py::gil_scoped_release release;

        m_events_mutex.lock();

        for (auto& event : m_events) {
            event.link_beat = next_link_beat(link_beat, event.beat, event.offset, event.origin);
        }

        // update m_link_beat here to ensure that interim sync events will not be scheduled later
        m_link_beat.store(link_beat, std::memory_order_relaxed);

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
    py::object m_loop;
};

struct Link {
    Link(double bpm, py::object loop = py::none()) : m_link(bpm), m_loop(loop) {
        if (m_loop.is_none()) {
            m_loop = py::module_::import("asyncio").attr("get_running_loop")();
        } else {
            py::warnings::warn("The 'loop' parameter is deprecated and will be removed in future versions of aalink",
               PyExc_DeprecationWarning, 1);
        }

        m_scheduler = std::make_unique<Scheduler>(m_link, m_loop);
    }

    std::size_t num_peers() {
        return m_link.numPeers();
    }

    double beat() {
        auto link_state = m_link.captureAppSessionState();
        auto link_quantum = m_scheduler->m_link_quantum.load(std::memory_order_relaxed);
        return link_state.beatAtTime(m_link.clock().micros(), link_quantum);
    }

    double phase() {
        auto link_state = m_link.captureAppSessionState();
        return link_state.phaseAtTime(m_link.clock().micros(), m_scheduler->m_link_quantum.load(std::memory_order_relaxed));
    }

    std::chrono::microseconds time() {
        return m_link.clock().micros();
    }

    double quantum() {
        return m_scheduler->m_link_quantum.load(std::memory_order_relaxed);
    }

    void set_quantum(double quantum) {
        m_scheduler->m_link_quantum.store(quantum, std::memory_order_relaxed);
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
        auto link_quantum = m_scheduler->m_link_quantum.load(std::memory_order_relaxed);
        link_state.requestBeatAtTime(beat, m_link.clock().micros(), link_quantum);
        m_link.commitAppSessionState(link_state);

        m_scheduler->reschedule_sync_events(beat);
    }

    void force_beat(double beat) {
        auto link_state = m_link.captureAppSessionState();
        auto link_quantum = m_scheduler->m_link_quantum.load(std::memory_order_relaxed);
        link_state.forceBeatAtTime(beat, m_link.clock().micros(), link_quantum);
        m_link.commitAppSessionState(link_state);

        m_scheduler->reschedule_sync_events(beat);
    }

    void request_beat_at_start_playing_time(double beat) {
        auto link_state = m_link.captureAppSessionState();
        auto link_quantum = m_scheduler->m_link_quantum.load(std::memory_order_relaxed);
        link_state.requestBeatAtStartPlayingTime(beat, link_quantum);
        m_link.commitAppSessionState(link_state);

        m_scheduler->reschedule_sync_events(beat);
    }

    void set_is_playing_and_request_beat_at_time(bool playing, std::chrono::microseconds time, double beat) {
        auto link_state = m_link.captureAppSessionState();
        auto link_quantum = m_scheduler->m_link_quantum.load(std::memory_order_relaxed);
        link_state.setIsPlayingAndRequestBeatAtTime(playing, time, beat, link_quantum);
        m_link.commitAppSessionState(link_state);

        m_scheduler->reschedule_sync_events(beat);
    }

    void set_num_peers_callback(py::function callback) {
        m_link.setNumPeersCallback([this, callback](std::size_t num_peers) {
            // ensure the callback isn't called when the runtime is finalizing
            #if PY_VERSION_HEX < 0x30d0000
            if (!_Py_IsFinalizing()) {
            #else
            if (!Py_IsFinalizing()) {
            #endif
                py::gil_scoped_acquire acquire;

                auto loop_call_soon_threadsafe = this->m_loop.attr("call_soon_threadsafe");
                loop_call_soon_threadsafe(callback, num_peers);
            }
        });
    }

    void set_tempo_callback(py::function callback) {
        m_link.setTempoCallback([this, callback](double tempo) {
            // ensure the callback isn't called when the runtime is finalizing
            #if PY_VERSION_HEX < 0x30d0000
            if (!_Py_IsFinalizing()) {
            #else
            if (!Py_IsFinalizing()) {
            #endif
                py::gil_scoped_acquire acquire;

                auto loop_call_soon_threadsafe = this->m_loop.attr("call_soon_threadsafe");
                loop_call_soon_threadsafe(callback, tempo);
            }
        });
    }

    void set_start_stop_callback(py::function callback) {
        m_link.setStartStopCallback([this, callback](bool playing) {
            // ensure the callback isn't called when the runtime is finalizing
            #if PY_VERSION_HEX < 0x30d0000
            if (!_Py_IsFinalizing()) {
            #else
            if (!Py_IsFinalizing()) {
            #endif
                py::gil_scoped_acquire acquire;

                auto loop_call_soon_threadsafe = this->m_loop.attr("call_soon_threadsafe");
                loop_call_soon_threadsafe(callback, playing);
            }
        });
    }

    py::object sync(double beat, double offset, double origin) {
        auto future = m_loop.attr("create_future")();
        m_scheduler->schedule_sync(future, beat, offset, origin);
        return future;
    }

    ableton::Link m_link;
    py::object m_loop;
    std::unique_ptr<Scheduler> m_scheduler;
};

PYBIND11_MODULE(aalink, m) {
    py::class_<Link>(m, "Link")
        .def(py::init<double, py::object>(), py::arg("bpm"), py::arg("loop") = py::none())
        .def_property_readonly("num_peers", &Link::num_peers)
        .def_property_readonly("beat", &Link::beat)
        .def_property_readonly("phase", &Link::phase)
        .def_property_readonly("time", &Link::time)
        .def_property("quantum", &Link::quantum, &Link::set_quantum)
        .def_property("enabled", &Link::enabled, &Link::set_enabled)
        .def_property("start_stop_sync_enabled", &Link::start_stop_sync_enabled, &Link::set_start_stop_sync_enabled)
        .def_property("tempo", &Link::tempo, &Link::set_tempo)
        .def_property("playing", &Link::playing, &Link::set_playing)
        .def("request_beat", &Link::request_beat)
        .def("force_beat", &Link::force_beat)
        .def("request_beat_at_start_playing_time", &Link::request_beat_at_start_playing_time)
        .def("set_is_playing_and_request_beat_at_time", &Link::set_is_playing_and_request_beat_at_time)
        .def("set_num_peers_callback", &Link::set_num_peers_callback)
        .def("set_tempo_callback", &Link::set_tempo_callback)
        .def("set_start_stop_callback", &Link::set_start_stop_callback)
        .def("sync", &Link::sync, py::arg("beat"), py::arg("offset") = 0, py::arg("origin") = 0);
}
