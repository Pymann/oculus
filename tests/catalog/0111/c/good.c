int weight(int code)
{
    int result = 0;
    switch (code) {
    case 1:
    case 2:
        result = 10;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}
