#ifndef PINTOS_TEST_H
#define PINTOS_TEST_H

#include "threads/synch.h"

static void
printf_test(void)
{
  int64_t start;
  int i;
  printf("%s():\n", __func__);
  start = timer_ticks();
  for (i = 0; i < 1000; i++) {
    printf("%d\n", i);
  }
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
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

static void
fibo_recursive_test(void)
{
  int64_t start;
  printf("%s():\n", __func__);
  start = timer_ticks();
  fibo_rec(32);   //39
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
}

static long long
bubsort (long long len)
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
  return len;
}

static void
bubsort_test(void)
{
  int64_t start;
  printf("%s():\n", __func__);
  start = timer_ticks();
  bubsort(20000);
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
}

static long long
emptyloop (long long numiter)
{
#define MAGIC_NUMBER 0x54321	/* To identify code fragments in assembly. */
#define MAX(a, b) ((a) < (b) ? (b) : (a))
  long long i;

  for (i = 0; i < MAX(MAGIC_NUMBER,numiter); i++);
  return i;
}

static void
emptyloop_test(void)
{
  int64_t start;
  printf("%s():\n", __func__);
  start = timer_ticks();
  emptyloop(400000000);
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
}

static long long
compute_fibo_iter (long long n)
{
  long long fibo_cur, fibo_prev;
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

static void
fibo_iter_test (void)
{
  int64_t start;
  long long n = 400000000;
  printf("%s():\n", __func__);
  start = timer_ticks();
  compute_fibo_iter (n);
  printf("%s(): elapsed=%lld\n", __func__, timer_elapsed(start));
}

static long long
hanoi1 (int n)
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

  return num_moves;
}

static long long
hanoi2 (int n)
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
  return num_moves;
}

static long long
hanoi3 (int n)
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
  return num_moves;
}

static void
hanoi_test(int algo)
{
  int64_t start;
  int n = 28;
  printf("%s():\n", __func__);
  start = timer_ticks();
  switch (algo) {
    case 1: hanoi1(n); break;
    case 2: hanoi2(n); break;
    case 3: hanoi3(n); break;
    default: ASSERT(0);
  }
  printf("%s(%d): elapsed=%lld\n", __func__, algo, timer_elapsed(start));
}

static void
euclid_algo(long long num)
{
  long long i, m, n;
  int64_t start;
  start = timer_ticks();

  m = (((long long)random_ulong()) << 32) | ((long long)random_ulong());
  n = (((long long)random_ulong()) << 32) | ((long long)random_ulong());
  for (i = 0; i < num; i++) {
    long long a, b;
    a = m>n?m:n; b = m<=n?m:n;
    while (1) {
      long long r;
      if (b == 0) {
        //printf("GCD(%lld,%lld)=%lld\n", m, n, a);
        break;
      } else {
        long long t;
        r = a/b;
        t = b;
        b = a - r*b;
        a = t;
      }
    }
  }
  printf("%s: elapsed=%lld\n", __func__, timer_elapsed(start));
}

static void
sieve_of_erastothenes(void)
{
  long long i, num, base, arrsize;
  char *array;
  int64_t start;
#define test_bit(n) (array[i/8] & (1 << (i%8)))
#define clear_bit(n) do { array[i/8] &= ~(1 << (i%8)); } while (0)

  num = 800000;
  printf("%s():\n", __func__);
  arrsize = (num + 7)/8;
  array = malloc(arrsize);
  ASSERT(array);
  memset(array, 0xff, arrsize);

  start = timer_ticks();
  for (base = 2; base < num; base++) {
    for (i = base*base; i < num; i++) {
      if (test_bit(i) && (i % base) == 0) {
        clear_bit(i);
      }
    }
  }
  printf("%s: elapsed=%lld\n", __func__, timer_elapsed(start));
}

struct queue
{
  char *arr;
  int queue_size;
  struct semaphore chars, holes;
  struct lock queue_head_lock, queue_tail_lock;
  int head, tail;
};

#define PRODCON_FINISH -1
struct prodcon_struct {
  int64_t num_elements;
  struct queue *queue;
  struct semaphore *done_sema;
};

static void
producer_thread(void *aux)
{
  struct prodcon_struct *prodcon_struct = aux;
  struct semaphore *chars = &prodcon_struct->queue->chars;
  struct semaphore *holes = &prodcon_struct->queue->holes;
  int queue_size = prodcon_struct->queue->queue_size;
  struct lock *queue_head_lock = &prodcon_struct->queue->queue_head_lock;
  char *queue = prodcon_struct->queue->arr;
  int64_t i;
  for (i = 0; i < prodcon_struct->num_elements; i++) {
    //printf("%s() %d:\n", __func__, __LINE__);
    sema_down(holes);
    //printf("%s() %d:\n", __func__, __LINE__);
    lock_acquire(queue_head_lock);
    ASSERT(queue[prodcon_struct->queue->head] == 0);
    //printf("%s(): setting element %d to %d\n", __func__, prodcon_struct->queue->head, i+1);
    queue[prodcon_struct->queue->head] = (i%254)+1;
    prodcon_struct->queue->head = (prodcon_struct->queue->head+1) % queue_size;
    lock_release(queue_head_lock);
    sema_up(chars);
  }
  sema_up(prodcon_struct->done_sema);
}

static void
consumer_thread(void *aux)
{
  struct prodcon_struct *prodcon_struct = aux;
  struct lock *queue_tail_lock = &prodcon_struct->queue->queue_tail_lock;
  struct semaphore *chars = &prodcon_struct->queue->chars;
  struct semaphore *holes = &prodcon_struct->queue->holes;
  int queue_size = prodcon_struct->queue->queue_size;
  char *queue = prodcon_struct->queue->arr;
  while (1) {
    char val;
    //printf("%s() %d:\n", __func__, __LINE__);
    sema_down(chars);
    lock_acquire(queue_tail_lock);
    val = queue[prodcon_struct->queue->tail];
    //printf("%s(): getting element %d (val %d)\n",__func__,prodcon_struct->queue->tail, val);
    ASSERT(val);
    queue[prodcon_struct->queue->tail] = 0;
    prodcon_struct->queue->tail = (prodcon_struct->queue->tail+1) % queue_size;
    lock_release(queue_tail_lock);
    sema_up(holes);
    if (val == PRODCON_FINISH) {
      break;
    }
  }
  sema_up(prodcon_struct->done_sema);
}

static void
producer_consumer(int num_producers, int num_consumers,
    int64_t num_elements, int num_queues, int queue_size)
{
  tid_t *producers, *consumers;
  struct prodcon_struct *prod_struct, *con_struct;
  struct semaphore done_sema;
  struct queue *queue;
  int64_t start;
  int i;

  start = timer_ticks();
  ASSERT((num_producers % num_queues) == 0)
  ASSERT((num_consumers % num_queues) == 0)

  producers = malloc(sizeof(tid_t)*num_producers);
  consumers = malloc(sizeof(tid_t)*num_consumers);
  queue = malloc(sizeof(struct queue)*num_queues);
  prod_struct = malloc(sizeof(struct prodcon_struct)*num_producers);
  con_struct = malloc(sizeof(struct prodcon_struct)*num_consumers);
  sema_init(&done_sema, 0);

  for (i = 0; i < num_queues; i++) {
    queue[i].arr = malloc(queue_size);
    ASSERT(queue[i].arr);
    memset(queue[i].arr, 0, queue_size);
    queue[i].queue_size = queue_size;
    queue[i].head = queue[i].tail = 0;
    sema_init(&queue[i].chars, 0);
    sema_init(&queue[i].holes, queue_size);
    lock_init(&queue[i].queue_head_lock);
    lock_init(&queue[i].queue_tail_lock);
  }

  for (i = 0; i < num_producers; i++) {
    char name[64];
    snprintf(name, sizeof name, "producer%d", i);
    prod_struct[i].num_elements = num_elements;
    prod_struct[i].done_sema = &done_sema;
    prod_struct[i].queue = &queue[i % num_queues];
    //printf("%s(): creating producer thread %d\n", __func__, i);
    producers[i] = thread_create(name, PRI_DEFAULT, producer_thread,
        &prod_struct[i]);
    ASSERT(producers[i] != TID_ERROR);
  }
  for (i = 0; i < num_consumers; i++) {
    char name[64];
    snprintf(name, sizeof name, "consumer%d", i);
    con_struct[i].num_elements = -1;
    con_struct[i].done_sema = &done_sema;
    //sema_init(&con_struct[i].sema, 0);
    con_struct[i].queue = &queue[i % num_queues];
    //printf("%s(): creating consumer thread %d\n", __func__, i);
    consumers[i] = thread_create(name, PRI_DEFAULT, consumer_thread,
        &con_struct[i]);
    ASSERT(consumers[i] != TID_ERROR);
  }

  for (i = 0; i < num_producers; i++) {
    sema_down(&done_sema);
  }
  //printf("all producers done.\n");
  for (i = 0; i < num_consumers; i++) {
    int q = i % num_queues;
    sema_down(&queue[q].holes);
    queue[q].arr[queue[q].head] = PRODCON_FINISH;
    queue[q].head = (queue[q].head+1) % queue[q].queue_size;
    sema_up(&queue[q].chars);
  }
  for (i = 0; i < num_consumers; i++) {
    sema_down(&done_sema);
  }
  free(producers);
  free(consumers);
  free(prod_struct);
  free(con_struct);
  for (i = 0; i < num_queues; i++) {
    free(queue[i].arr);
  }
  free(queue);

  printf("%s: num_elements=%lld, num_queues=%d, queue_size=%d, "
      "num_producers=%d, num_consumers=%d, "
      "elapsed=%lld\n", __func__, num_elements, num_queues, queue_size,
      num_producers, num_consumers, timer_elapsed(start));
}

static void
producer_consumer_test(void)
{
  int num_queues, queue_size, num_consumers, num_producers;
  int64_t num_elements;
  int64_t start;
  printf("%s():\n", __func__);
  num_elements = 100000;
  producer_consumer(100, 100, num_elements, 100, 1000);
  producer_consumer(100, 100, num_elements, 10, 1000);
#if 0
  for (num_queues = 1; num_queues <= 256; num_queues *= 16) {
    for (queue_size = 2; queue_size <= 128; queue_size *= 8) {
      for (num_producers = num_queues*1;
           num_producers <= num_queues*64;
           num_producers *= 8) {
        for (num_consumers = num_queues*1;
            num_consumers <= num_queues*64;
            num_consumers *= 8) {
          start = timer_ticks();
          producer_consumer(num_producers, num_consumers, num_elements,
              num_queues, queue_size);
          printf("%s: num_elements=%d, num_queues=%d, queue_size=%d, "
              "num_producers=%d, num_consumers=%d, "
              "elapsed=%lld\n", __func__, num_elements, num_queues, queue_size,
              num_producers, num_consumers, timer_elapsed(start));
        }
      }
    }
  }
#endif
}

struct readerwriter_struct {
  struct lock lock;
  int active_readers, active_writers;
  int waiting_readers, waiting_writers;
  struct condition ok_to_read, ok_to_write;
  struct semaphore done;
};

static int db;
static void
access_db_readonly(void)
{
  int val;
  val = db;
  //printf("val=%d\n", val);
}

static void
access_db_readwrite(void)
{
  db++;
}

static void
reader_thread(void *aux)
{
  struct readerwriter_struct *rw_struct = aux;
  lock_acquire(&rw_struct->lock);
  while ((rw_struct->active_writers + rw_struct->waiting_writers) > 0) {
    rw_struct->waiting_readers++;
    cond_wait(&rw_struct->ok_to_read, &rw_struct->lock);
    rw_struct->waiting_readers--;
  }
  rw_struct->active_readers++;
  lock_release(&rw_struct->lock);
  access_db_readonly();
  lock_acquire(&rw_struct->lock);
  rw_struct->active_readers--;
  if (rw_struct->active_readers == 0 && rw_struct->waiting_writers > 0) {
    cond_signal(&rw_struct->ok_to_write, &rw_struct->lock);
  }
  lock_release(&rw_struct->lock);
  sema_up(&rw_struct->done);
}

static void
writer_thread(void *aux)
{
  struct readerwriter_struct *rw_struct = aux;
  lock_acquire(&rw_struct->lock);
  while ((rw_struct->active_writers + rw_struct->active_readers) > 0) {
    rw_struct->waiting_writers++;
    cond_wait(&rw_struct->ok_to_write, &rw_struct->lock);
    rw_struct->waiting_writers--;
  }
  rw_struct->active_writers++;
  lock_release(&rw_struct->lock);
  access_db_readwrite();
  lock_acquire(&rw_struct->lock);
  rw_struct->active_writers--;
  if (rw_struct->waiting_writers > 0) {
    cond_signal(&rw_struct->ok_to_write, &rw_struct->lock);
  } else if (rw_struct->waiting_readers > 0) {
    cond_broadcast(&rw_struct->ok_to_read, &rw_struct->lock);
  }
  lock_release(&rw_struct->lock);
  sema_up(&rw_struct->done);
}

static void
reader_writer(int num_readers, int num_writers)
{
  struct readerwriter_struct rw_struct;
  int64_t start;
  int i;

  start = timer_ticks();
  lock_init(&rw_struct.lock);
  cond_init(&rw_struct.ok_to_read);
  cond_init(&rw_struct.ok_to_write);
  sema_init(&rw_struct.done, 0);
  rw_struct.active_readers = 0;
  rw_struct.active_writers = 0;
  rw_struct.waiting_readers = 0;
  rw_struct.waiting_writers = 0;
  for (i = 0; i < num_writers; i++) {
    char name[64];
    snprintf(name, sizeof name, "writer%d", i);
    thread_create(name, PRI_DEFAULT, writer_thread, &rw_struct);
  }
  for (i = 0; i < num_readers; i++) {
    char name[64];
    snprintf(name, sizeof name, "reader%d", i);
    thread_create(name, PRI_DEFAULT, reader_thread, &rw_struct);
  }
  for (i = 0; i < num_readers; i++) {
    char name[64];
    snprintf(name, sizeof name, "reader%d", i);
    thread_create(name, PRI_DEFAULT, reader_thread, &rw_struct);
  }
  for (i = 0; i < num_writers; i++) {
    char name[64];
    snprintf(name, sizeof name, "writer%d", i);
    thread_create(name, PRI_DEFAULT, writer_thread, &rw_struct);
  }
  for (i = 0; i < 2*(num_readers+num_writers); i++) {
    sema_down(&rw_struct.done);
  }
  printf("%s: num_readers=%d, num_writers=%d, elapsed=%lld\n", __func__,
      num_readers, num_writers, timer_elapsed(start));
}

static void
reader_writer_test(void)
{
  int num_readers, num_writers;
  int64_t start;
  printf("%s():\n", __func__);
  reader_writer(100, 2000);
  reader_writer(2000, 100);
  reader_writer(1000, 1000);
}

static void
euclid_test(void)
{
  printf("%s():\n", __func__);
  euclid_algo(1000000);
}

static void
producer_no_locks_thread(void *aux)
{
  struct prodcon_struct *prodcon_struct = aux;
  struct queue *queue = prodcon_struct->queue;
  int queue_size = queue->queue_size;
  char *arr = queue->arr;
  int i;
  for (i = 0; i < prodcon_struct->num_elements; i++) {
    while ((queue->head - queue->tail) == queue_size);
    arr[queue->head] = (i % 254) + 1;
    queue->head = (queue->head + 1) % queue_size;
  }
  while ((queue->head - queue->tail) == queue_size);
  arr[queue->head] = PRODCON_FINISH;
  queue->head = (queue->head + 1) % queue_size;

  sema_up(prodcon_struct->done_sema);
}

static void
consumer_no_locks_thread(void *aux)
{
  struct prodcon_struct *prodcon_struct = aux;
  struct queue *queue = prodcon_struct->queue;
  int queue_size = queue->queue_size;
  char *arr = queue->arr;
  while (1) {
    char val;
    while (queue->tail == queue->head);
    val = arr[queue->tail];
    queue->tail = (queue->tail + 1) % queue_size;
    if (val == PRODCON_FINISH) {
      break;
    }
  }
  sema_up(prodcon_struct->done_sema);
}

static void
producer_consumer_no_locks(int64_t num_elements, int num_queues, int queue_size)
{
  tid_t *producers, *consumers;
  struct prodcon_struct *prod_struct, *con_struct;
  struct semaphore done_sema;
  struct queue *queue;
  int64_t start;
  int i;

  start = timer_ticks();
  producers = malloc(sizeof(tid_t)*num_queues);
  consumers = malloc(sizeof(tid_t)*num_queues);
  queue = malloc(sizeof(struct queue)*num_queues);
  prod_struct = malloc(sizeof(struct prodcon_struct)*num_queues);
  con_struct = malloc(sizeof(struct prodcon_struct)*num_queues);
  sema_init(&done_sema, 0);

  for (i = 0; i < num_queues; i++) {
    queue[i].arr = malloc(queue_size);
    ASSERT(queue[i].arr);
    memset(queue[i].arr, 0, queue_size);
    queue[i].queue_size = queue_size;
    queue[i].head = queue[i].tail = 0;
  }

  for (i = 0; i < num_queues; i++) {
    char name[64];
    snprintf(name, sizeof name, "producer%d", i);
    prod_struct[i].num_elements = num_elements;
    prod_struct[i].done_sema = &done_sema;
    prod_struct[i].queue = &queue[i];
    producers[i] = thread_create(name, PRI_DEFAULT, producer_no_locks_thread,
        &prod_struct[i]);
    ASSERT(producers[i] != TID_ERROR);
  }
  for (i = 0; i < num_queues; i++) {
    char name[64];
    snprintf(name, sizeof name, "consumer%d", i);
    con_struct[i].num_elements = -1;
    con_struct[i].done_sema = &done_sema;
    con_struct[i].queue = &queue[i];
    consumers[i] = thread_create(name, PRI_DEFAULT, consumer_no_locks_thread,
        &con_struct[i]);
    ASSERT(consumers[i] != TID_ERROR);
  }
  for (i = 0; i < 2*num_queues; i++) {
    sema_down(&done_sema);
  }
  //printf("all producers and consumers done.\n");
  free(producers);
  free(consumers);
  free(prod_struct);
  free(con_struct);
  for (i = 0; i < num_queues; i++) {
    free(queue[i].arr);
  }
  free(queue);
  printf("%s: num_elements=%lld, num_queues=%d, queue_size=%d, "
      "elapsed=%lld\n", __func__, num_elements, num_queues, queue_size,
      timer_elapsed(start));
}

static void
producer_consumer_no_locks_test(void)
{
  int num_queues, queue_size;
  int64_t num_elements;
  printf("%s():\n", __func__);
  num_elements = 1000000;
  for (num_queues = 1; num_queues <= 16; num_queues *= 4) {
    for (queue_size = 2; queue_size <= 128; queue_size *= 4) {
      producer_consumer_no_locks(num_elements, num_queues, queue_size);
    }
  }
}

static void
pintos_test(void)
{
  printf_test();
  fibo_recursive_test();
  bubsort_test();
  emptyloop_test();
  fibo_iter_test();
  hanoi_test(1);
  hanoi_test(2);
  hanoi_test(3);
  euclid_test();
  sieve_of_erastothenes();
  //producer_consumer_test();
  //reader_writer_test();
  //producer_consumer_no_locks_test();
}

#endif    /* pintos_test.h */
