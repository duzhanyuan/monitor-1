include /home/sbansal/csl862-hw2/monee/config-host.mak
TEST_VARIANTS_ALL=default tu1 numswap5 numtc10 record rr lockstep
TESTS_QUICKONLY=1
TEST_SIMULATOR=tapas
TEST_CONFIGURE_FLAGS=""
numswap5-test: TEST_CONFIGURE_FLAGS= max_num_swap_pages=5
tu1-test: TEST_CONFIGURE_FLAGS= tu_size=1
record-test: TEST_SIMULATOR=tapas-record
rr-test: TEST_SIMULATOR=tapas-rr
lockstep-test: TEST_CONFIGURE_FLAGS=rec_print_freq=1000000
lockstep-test: TEST_SIMULATOR=tapas-lockstep
BINUTILS_DIST=binutils-2.19
BINUTILS_DIST_PATH=$(abspath /home/sbansal/csl862-hw2/monee/tars/binutils-2.19.tar.bz2)
