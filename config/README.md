# Configuracao publica de release

Este diretorio recebe somente `update-release-public.json`, gerado por
`tools/updater.py keygen`. Chaves privadas, seeds e senhas devem permanecer
fora do repositorio.

## Canal remoto

`update-remote.json` usa o formato `zephyros-update-remote-v2` descrito em
`config/update-remote.schema.json`. O header
`src/include/core/update_remote_config.h` e derivado dele por
`tools/updater.py sync-remote` e deve ser conferido com `check-remote`. O
arquivo versiona o canal Stable, o fallback HTTP U5 e o endpoint HTTPS do
GitHub (`github_api_url`, owner, repository, template por `{owner}`, `{repo}`
e `{tag}`), a versao da API e os nomes dos assets `release.json`,
`release.zum` e `update.zephyrosupd`. Nenhum token GitHub ou certificado/chave
privada de fixture pertence a este repositorio.
