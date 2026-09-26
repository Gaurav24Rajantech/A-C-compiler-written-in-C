int add6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    printf("add6 = %d\n", add6(1, 2, 3, 4, 5, 6));
    printf("10! = %d\n", factorial(10));

    int i;
    int count;
    count = 0;
    for (i = 2; i < 30; i = i + 1) {
        int j;
        int is_prime;
        is_prime = 1;
        for (j = 2; j * j <= i; j = j + 1) {
            if (i % j == 0) {
                is_prime = 0;
            }
        }
        if (is_prime) {
            printf("%d ", i);
            count = count + 1;
        }
    }
    printf("\nfound %d primes\n", count);
    return 0;
}
