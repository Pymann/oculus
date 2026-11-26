int scale(int code)
{
    int result = 1;
    switch (code) {
    case 1:
        result = 10;
        break;
    case 2:
        result = 20;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}
