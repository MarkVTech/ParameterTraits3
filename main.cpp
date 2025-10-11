#include "ParameterTraits.h"
#include <atomic>
#include <array>
#include <chrono>
#include <iostream>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

//
// SImple lock-free SPSC (Single Producer Single Consumer ring buffer
//
template <typename T, std::size_t CapacityPow2>
class SPSCQueue
{
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "Capacity must be power of two.");

public:
    bool try_push(const T& v) { return try_push(T(v)); }

    bool try_push(T&& v)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        buf_[head] = std::move(v);
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> try_pop()
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return std::nullopt;
        T v = std::move(buf_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return v;
    }

private:
    static constexpr std::size_t mask_ = CapacityPow2 - 1;
    std::array<T, CapacityPow2> buf_{};
    std::atomic<std::size_t> head_{0}, tail_{0};
};

//
// Messages (carry full tag types)
//
struct Stop {};

template <typename ParamTag>
struct SetParam
{
    ParamTag value{};
};

//
// NOT ideal - variant template parameters could grow a lot with
// a lot of parameters. BUT, we'll stick with this for now because
// it is simple for demo.  Maybe use a type-erased Msg class later.
//
using Msg = std::variant<SetParam<TemperatureSetpoint>,
                         SetParam<HighTemperatureAlarm>,
                         SetParam<FanDutyCycle>,
                         Stop>;

// ------------------------
// Parameter table using your tag types
// ------------------------
struct ParameterTable
{
    TemperatureSetpoint t { param_default<TemperatureSetpoint>() };
    HighTemperatureAlarm a { param_default<HighTemperatureAlarm>() };
    FanDutyCycle         f { param_default<FanDutyCycle>() };

    template <typename Tag>
    void set(const Tag& v)
    {
        if constexpr (std::is_same_v<Tag, TemperatureSetpoint>)
        {
            t = v;
        }
        else if constexpr (std::is_same_v<Tag, HighTemperatureAlarm>)
        {
            a = v;
        }
        else if constexpr (std::is_same_v<Tag, FanDutyCycle>)
        {
            f = v;
        }
    }

    void print() const
    {
        char buf[32]{};
        int n = 0;
        n = param_serialize(t, buf, sizeof(buf));
        std::cout << "Params { Tset=" << (n ? buf : "?");
        n = param_serialize(a, buf, sizeof(buf));
        std::cout << ", HighAlarm=" << (n ? buf : "?");
        n = param_serialize(f, buf, sizeof(buf));
        std::cout << ", FanDuty=" << (n ? buf : "?") << "% }\n";
    }
};

// ------------------------
// Consumer
// ------------------------
class ParamWorker
{
public:
    explicit ParamWorker(SPSCQueue<Msg, 1024>& q, ParameterTable& tbl) : q_(q), table_(tbl) {}

    void start()
    {
        running_.store(true);
        worker_ = std::thread([this] { run(); });
    }
    void join() { if (worker_.joinable()) worker_.join(); }

private:
    void run()
    {
        while (running_.load())
        {
            if (auto m = q_.try_pop()) std::visit([this](auto&& x){ handle(x); }, *m);
            else std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    void handle(const Stop&) { running_.store(false); }

    template <typename Tag>
    void handle(const SetParam<Tag>& s)
    {
        if (param_validate(s.value))
        {
            table_.set(s.value);
        }
        else
        {
            char buf[32]{};
            [[maybe_unused]] int n = param_serialize(s.value, buf, sizeof(buf));
            std::cout << "[Reject] " << param_name<Tag>() << " value\n";
        }
    }

    SPSCQueue<Msg, 1024>& q_;
    ParameterTable& table_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

int main()
{
    ParameterTable params;
    params.print();

    SPSCQueue<Msg, 1024> q;
    ParamWorker worker(q, params);
    worker.start();

    std::thread producer([&]
    {
        // Aggregate init with your tag types
        q.try_push(Msg{SetParam<TemperatureSetpoint>{ TemperatureSetpoint{ 37.5f } }});
        q.try_push(Msg{SetParam<HighTemperatureAlarm>{ HighTemperatureAlarm{ 90.0f } }});
        q.try_push(Msg{SetParam<FanDutyCycle>{ FanDutyCycle{ 45.0f } }});

        // Invalid examples (will be rejected)
        q.try_push(Msg{SetParam<FanDutyCycle>{ FanDutyCycle{ 200.0f } }});

        q.try_push(Msg{Stop{}});
    });

    producer.join();
    worker.join();
    params.print();
    return 0;
}
