void wait_ready(volatile int &ready)
{
    while (ready == 0)
        ;   // busy wait
}
