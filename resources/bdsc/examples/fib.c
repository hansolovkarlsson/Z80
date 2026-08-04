int fib(n)
int n;
{
    if (n < 2)
        return n;
    return fib(n-1) + fib(n-2);
}

main()
{
    int i;
    for (i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib(i));
    }
}
