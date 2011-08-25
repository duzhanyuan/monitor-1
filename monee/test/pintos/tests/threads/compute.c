/* Compute-intensive kernels. */

#include <stdio.h>
#include <random.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "devices/timer.h"

static long long
do_bubsort (long long len)
{
  long long *a = malloc (len*sizeof(long long));
  if (!a) {
    printf ("%s(): array allocation of size %lld failed.\n", __func__,
        len*sizeof(long long));
    ASSERT(0);
  }

  int i, j;

  for (i = 0; i < len; i++) {
    a[i] = (((long long)random_ulong()) << 32) | ((long long)random_ulong()) ;
  }

  if (len <= 10) {
    printf ("Printing Input array:\n");
    for (i = 0; i < len; i++)
      printf ("a[%d] = %lld\n", i, a[i]);
  }

  for (i = 0; i < len; i++) {
    for (j = 0; j < len - i - 1; j++) {
      if (a[j] > a[j+1]) {
        long long tmp = a[j+1];
        a[j+1] = a[j];
        a[j] = tmp;
      }
    }
  }

  if (len <= 10) {
    printf ("Printing Sorted array:\n");
    for (i = 0; i < len; i++)
      printf ("a[%d] = %lld\n", i, a[i]);
  }
	free(a);
  return len;
}

void
bubsort (void)
{
  int64_t start;
  printf("%s() entry:\n", __func__);
  start = timer_ticks();
  do_bubsort(40000);
  //do_bubsort(1000);//fast
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
	printf("%s(): Printing statistics:\n", __func__);
	print_stats();
}

static uint64_t
fibo_rec(int n)
{
  if (n == 0) {
    return 1;
  } else if (n == 1) {
    return 1;
  } else {
    return fibo_rec(n - 1) + fibo_rec(n - 2);
  }
}

void
fibo_recursive (void)
{
  int64_t start;
  uint64_t volatile ret;
  printf("%s():\n", __func__);
  start = timer_ticks();
  ret = fibo_rec(39);
  //fibo_rec(29);//fast
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
	printf("%s(): Printing statistics:\n", __func__);
	print_stats();
}

static long long
do_fibo_iter (long long n)
{
  long long volatile fibo_cur, fibo_prev;
  int i, tmp;

  if (n < 3) {
    return 1;
  }

  fibo_cur = 1;
  fibo_prev = 1;

  for (i = 3; i <= n; i++) {
    tmp = fibo_cur;
    fibo_cur += fibo_prev;
    fibo_prev = tmp;
  }
  return fibo_cur;
}

void
fibo_iter (void)
{
  int64_t start;
  long long n = 400000000;
  //long long n = 40000000;//fast
  printf("%s():\n", __func__);
  start = timer_ticks();
  do_fibo_iter (n);
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
	printf("%s(): Printing statistics:\n", __func__);
	print_stats();
}

static long long
do_hanoi1 (int n)
{
  /* Kolar's Hanoi Tower algorithm no. 1 */
  /* http://hanoitower.mkolar.org/algo.html */
#define ALLO(x) do { \
  if((x = (int *)malloc((n+3) * sizeof(int))) == NULL) {\
    printf(#x " allocation failed!\n"); ASSERT(0); }\
} while (0)

  int i, *a, *b, *c, *p, *fr, *to, *sp, n1, n2;
  long long num_moves = 0;

  n1 = n+1;
  n2 = n+2;
  ALLO(a);
  ALLO(b);
  ALLO(c);

  a[0] = 1; b[0] = c[0] = n1;
  a[n1] = b[n1] = c[n1] = n1;
  a[n2] = 1; b[n2] = 2; c[n2] = 3;
  for(i=1; i<n1; i++) {
    a[i] = i; b[i] = c[i] = 0;
  }

  fr = a;
  if(n&1) { to = c; sp = b; }
  else    { to = b; sp = c; }

  while(c[0]>1) {
    i = fr[fr[0]++];
    if (n <= 5)
    {
      printf("move disc %d from %d to %d\n", i, fr[n2], to[n2]);
    }
    num_moves++;
    p=sp;
    if((to[--to[0]] = i)&1) {
      sp=to;
      if(fr[fr[0]] > p[p[0]]) { to=fr; fr=p; }
      else to=p;
    } else { sp=fr; fr=p; }
  }
  free(a);
  free(b);
  free(c);

  return num_moves;
}

static long long
do_hanoi2 (int n)
{
  /*  Er's LLHanoi Hanoi Tower loop less algorithm */
  int i, dir, *D, *s, n1;

  n1 = n+1;
  ALLO(D);
  ALLO(s);

  for(i=0; i<=n1; i++) {
    D[i] = 1; s[i] = i+1;
  }

  dir = n&1;

  long long num_moves = 0;
  for(;;) {
    i = s[0];
    if(i>n) break;
    int prevD = D[i];
    D[i]=(D[i]+(i&1?dir:1-dir))%3+1;
    if (n <=5)
    {
      printf ("move disc %d from %d to %d\n", i, prevD, D[i]);
    }
    num_moves++;
    s[0] = 1;
    s[i-1] = s[i];
    s[i] = i+1;
  }
	free(D);
	free(s);
  return num_moves;
}

static long long
do_hanoi3 (int n)
{
#define TOWER_ID(x) (((x) == a)?1:((x)==b)?2:((x)==c)?3:-1)
  int i, *a, *b, *c, *p, *o1, *o2, *e, n1;

  n1 = n+1;
  ALLO(a);
  ALLO(b);
  ALLO(c);

  a[0] = 1; b[0] = c[0] = n1;
  a[n1] = n1; b[n1] = n+2; c[n1] = n+3;
  for(i=1; i<n1; i++) {
    a[i] = i; b[i] = c[i] = 0;
  }

  o1 = a;
  if(n&1) { o2 = b; e = c; }
  else    { o2 = c; e = b; }

  long long num_moves = 0;
  while(*c>1) {
    num_moves++;
    if(o1[*o1] > e[*e])
    {
      if (n <=5)
      {
        printf ("move disc from %d to %d\n", TOWER_ID(e), TOWER_ID(o1));
      }
      o1[--(*o1)] = e[(*e)++];
    } else {
      if (n <=5) {
        printf ("move disc from %d to %d\n", TOWER_ID(o1), TOWER_ID(e));
      }
      e[--(*e)] = o1[(*o1)++];
    }
    p = e; e = o1; o1 = o2; o2 = p;
  }
	free(a);
	free(b);
	free(c);
  return num_moves;
}

static void
hanoi_test(int algo)
{
  int64_t start;
  int n = 32;
  //int n = 22;//fast
  printf("%s():\n", __func__);
  start = timer_ticks();
  switch (algo) {
    case 1: do_hanoi1(n); break;
    case 2: do_hanoi2(n); break;
    case 3: do_hanoi3(n); break;
    default: ASSERT(0);
  }
  printf("%s(%d): elapsed=%lld\n", __func__, algo, timer_elapsed(start));
	printf("%s(): Printing statistics:\n", __func__);
	print_stats();
}

void
hanoi1 (void)
{
	return hanoi_test(1);
}

void
hanoi2 (void)
{
	return hanoi_test(2);
}

void
hanoi3 (void)
{
	return hanoi_test(3);
}

void
printf_test(void)
{
  int64_t start;
  int i;
  printf("%s():\n", __func__);
  start = timer_ticks();
  for (i = 0; i < 2000; i++) {
    printf("%d\n", i);
  }
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
	printf("%s(): Printing statistics:\n", __func__);
	print_stats();
}

static long long
do_emptyloop (long long numiter)
{
#define MAGIC_NUMBER 0x54321	/* To identify code fragments in assembly. */
#define MAX(a, b) ((a) < (b) ? (b) : (a))
  long long volatile i;

  for (i = 0; i < MAX(MAGIC_NUMBER,numiter); i++);
  return i;
}

void
emptyloop (void)
{
  int64_t start;
  printf("%s():\n", __func__);
  start = timer_ticks();
  do_emptyloop(400000000);
  //do_emptyloop(40000000);//fast
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
	printf("%s(): Printing statistics:\n", __func__);
	print_stats();
}
