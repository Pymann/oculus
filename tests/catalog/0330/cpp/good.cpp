struct Timer {
    int ticks = 0;
};

int elapsed()
{
    Timer timer{};
    Timer other = Timer();
    return timer.ticks + other.ticks;
}
