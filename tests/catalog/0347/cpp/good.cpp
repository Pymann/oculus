#include <csignal>

volatile std::sig_atomic_t stop_requested = 0;

extern "C" void on_signal(int)
{
    stop_requested = 1;
}
