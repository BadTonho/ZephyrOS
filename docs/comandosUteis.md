Tag
git tag -a v0.4.5 -m "descrição"
git push origin <tag>

Teste continuo
make test-tst7-continuous-parallel QEMU_PARALLEL_WORKERS=6