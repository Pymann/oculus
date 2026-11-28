/* A hardware register: never referenced in code, but placed by the linker. */
volatile unsigned int watchdog_kick;

int idle(void)
{
    return 0;
}
