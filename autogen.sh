#!/bin/bash
aclocal \
    && autoheader \
    && automake --gnu --add-missing \
    && autoconf
