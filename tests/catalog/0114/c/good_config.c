void handle(int code, int *state)
{
    switch (code) {
    case 1:
        *state = 1;
        break;
#ifdef HANDLE_ALL_CODES
    default:
        *state = 0;
        break;
#endif
    }
}
