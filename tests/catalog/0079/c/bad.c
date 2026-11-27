struct handle;

int peek(struct handle *item)
{
    return *(int *)item;
}
