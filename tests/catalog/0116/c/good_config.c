int clock_kind(int clock_id)
{
    switch (clock_id) {
#if defined(CLOCK_KIND_REALTIME)
    case 1:
        return 10;
#endif
    default:
        return -1;
    }
}
