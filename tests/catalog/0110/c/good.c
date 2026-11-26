int grade(int score)
{
    int result = 0;
    if (score > 90) {
        result = 1;
    } else if (score > 50) {
        result = 2;
    } else {
        /* below 50: result stays 0 */
    }
    if (score < 0) {
        result = -1;   /* plain if: no else needed */
    }
    return result;
}
