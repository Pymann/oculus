void handle(int code, int *state)
{
    switch (code) {
    case 1:
        *state = 1;
        break;
    case 2:
        *state = 2;
        break;
    default:
        /* other codes leave the state unchanged */
        break;
    }
}
