int weight(int code)
{
    int result = 0;
    switch (code) {
    case 1:
        result = 10;
        if (result > 5) {
    case 2:
            result = 20;
        }
        break;
    default:
        break;
    }
    return result;
}
