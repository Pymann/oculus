static inline void swap(int *left, int *right)
{
    int keep = *left;
    *left = *right;
    *right = keep;
}

#define LIMIT 10
