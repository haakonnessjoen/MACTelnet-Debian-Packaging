#!/bin/sh
echo '#include <libintl.h>
#include <stdio.h>
int main(char **argv, int argc) {
	printf(gettext("test"));
  return 0;
}' > .tempfile.c
$CC -o .tempfile .tempfile.c && echo ok || echo fail
