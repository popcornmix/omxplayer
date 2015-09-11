#!/bin/bash
aclocal && autoconf && automake --add-missing --copy
if [ $? -eq 0 ]; then
	exit 0
fi

REQ_PACKAGES="autoconf automake pkg-config"
echo -e "[en] Somethin went wrong.
\tThis project needs following packages to compile: $REQ_PACKAGES
\tOr it could be someone else's fault.
[ko] 무엇인가 잘못되었다.
\t이 프로젝트는 컴파일을 위해 다음 패키지들이 필요합니다: $REQ_PACKAGES
\t아니면 다른 누군가의 잘못이거나." >& 2

exit 1

