#ifndef MIGRAPHX_GUARD_RTGLIB_CONTEXT_HPP
#define MIGRAPHX_GUARD_RTGLIB_CONTEXT_HPP

#include <migraphx/gpu/miopen.hpp>
#include <migraphx/gpu/rocblas.hpp>
#include <migraphx/gpu/hip.hpp>
#include <migraphx/env.hpp>
#include <migraphx/config.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace gpu {

MIGRAPHX_DECLARE_ENV_VAR(MIGRAPHX_ENABLE_NULL_STREAM)

struct hip_device
{
    using hip_event_ptr = MIGRAPHX_MANAGE_PTR(hipEvent_t, hipEventDestroy);

    hip_device() {}

    hip_device(std::size_t id, std::size_t n) : device_id(id) { add_streams(n); }

    struct stream
    {
        using hip_stream_ptr = MIGRAPHX_MANAGE_PTR(hipStream_t, hipStreamDestroy);

        stream() {}

        stream(std::size_t device_number) : id(device_number) {}

        void setup() { set_device(id); }

        static hip_stream_ptr create_stream()
        {
            hipStream_t result = nullptr;
            auto status = hipStreamCreate(&result);
            // auto status        = hipStreamCreateWithFlags(&result, hipStreamNonBlocking);

            if(status != hipSuccess)
                MIGRAPHX_THROW("Failed to allocate stream");
            return hip_stream_ptr{result};
        }

        hipStream_t get()
        {
            if(not enabled(MIGRAPHX_ENABLE_NULL_STREAM{}))
            {
                setup();
                if(s == nullptr)
                    s = create_stream();
                assert(s.get() != nullptr);
                return s.get();
            }
            return nullptr;
        }

        auto create_miopen_handle()
        {
            if(not enabled(MIGRAPHX_ENABLE_NULL_STREAM{}))
                return make_obj<miopen_handle>(&miopenCreateWithStream, get());
            else
                return make_obj<miopen_handle>(&miopenCreate);
        }

        auto get_miopen()
        {
            setup();
            if(mihandle == nullptr)
                mihandle = create_miopen_handle();
            assert(mihandle.get() != nullptr);
            return mihandle.get();
        }

        auto get_rocblas()
        {
            setup();
            if(rbhandle == nullptr)
                rbhandle = create_rocblas_handle_ptr(get());
            assert(rbhandle.get() != nullptr);
            return rbhandle.get();
        }

        void sync() const
        {
            if (s != nullptr)
                hipStreamSynchronize(s.get());
        }

        private:
        std::size_t id                      = 0;
        shared<hip_stream_ptr> s            = nullptr;
        shared<miopen_handle> mihandle      = nullptr;
        shared<rocblas_handle_ptr> rbhandle = nullptr;
    };

    static hip_event_ptr create_event()
    {
        hipEvent_t event;
        auto status = hipEventCreateWithFlags(&event, hipEventDisableTiming);
        if(status != hipSuccess)
            MIGRAPHX_THROW("Failed to creat event");
        return hip_event_ptr{event};
    }

    void add_streams(std::size_t num_of_streams)
    {
        assert(streams.empty());
        for(int i = 0; i < num_of_streams; ++i)
            streams.emplace_back(device_id);
    }

    std::size_t nstreams() const { return streams.size(); }

    stream& get_stream() { return streams.at(current_stream); }

    void set_stream(std::size_t n) { current_stream = n; }
    void create_events(int num_of_events)
    {
        for(int i = events.size(); i < num_of_events; ++i)
            events.emplace_back(create_event());
    }
    void record_event(int event)
    {
        create_events(event + 1);
        hipEventRecord(events.at(event).get(), streams.at(current_stream).get());
    }

    void wait_event(int event)
    {
        hipStreamWaitEvent(streams.at(current_stream).get(), events.at(event).get(), 0);
    }

    void sync() const
    {
        for(auto&& stream : streams)
            stream.sync();
    }

    private:
    std::size_t device_id      = 0;
    std::size_t current_stream = 0;
    std::vector<stream> streams;
    std::vector<shared<hip_event_ptr>> events;
};

struct context
{
    context(std::size_t device_id = 0, std::size_t n = 4)
        : current_device(std::make_shared<hip_device>(device_id, n))
    {
    }

    const hip_device& get_current_device() const
    {
        assert(current_device != nullptr);
        return *current_device;
    }

    hip_device& get_current_device()
    {
        assert(current_device != nullptr);
        return *current_device;
    }

    hip_device::stream& get_stream() { return get_current_device().get_stream(); }
    void set_stream(int n)
    {
        if(n >= 0)
            get_current_device().set_stream(n);
    }
    void create_events(int num_of_events)
    {
        get_current_device().create_events(num_of_events);
    }
    void record_event(int event) { get_current_device().record_event(event); }
    void wait_event(int event) { get_current_device().wait_event(event); }

    std::vector<argument> literals{};
    void finish() const
    {
        get_current_device().sync();
        gpu_sync();
    }

    private:
    // TODO: Make this a vector to support multiple devices
    std::shared_ptr<hip_device> current_device;
};
} // namespace gpu
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx

#endif
