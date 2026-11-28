#define CPU_PAUSE() asm("pause")

void pause_cpu()
{
    CPU_PAUSE();
}
