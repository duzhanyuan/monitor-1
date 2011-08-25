# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_USER_FAULTS => 1, [<<'EOF']);
(bad-jump3) begin
bad-jump3: exit(-1)
EOF
pass;
