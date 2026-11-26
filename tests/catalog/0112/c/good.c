int pick(int code, int flag)
{
    int result = 0;
    switch (code) {
    case 1:
        if (flag != 0) {
            result = 2;
        }
        break;
    default:
        result = 2;
        break;
    }
    return result;
}
