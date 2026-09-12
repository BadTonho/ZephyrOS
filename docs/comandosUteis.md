Tag
git tag -a v0.4.5 -m "descrição"
git push origin <tag>

Teste continuo
make test-tst7-continuous-parallel QEMU_PARALLEL_WORKERS=6

# SHELL5 - atualizacao do sistema
make test-shell5-host
make test-shell5-qemu SHELL5_QEMU_WORKERS=4 SHELL5_QEMU_SEED=2205
make test-shell5

# STO2 — sync, flush e transacoes host-only
make test-sto2-host

git pull
git ls-files --error-unmatch tools/tst7_continuous_runner.py
make test-tst7-continuous-parallel QEMU_PARALLEL_WORKERS=6
