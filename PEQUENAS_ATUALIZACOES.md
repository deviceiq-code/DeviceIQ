# Pequenas Atualizações

Lista de pequenas alterações acumuladas ao longo do tempo. Só devem ser
implementadas quando explicitamente solicitado ("processar as pequenas
atualizações" ou equivalente) - não implementar por conta própria.

- [x] Auto refresh do log (log.html) deve salvar o estado (ligado/desligado) em cookie de usuário
- [x] No log (log.html), em View, remover o botão "Refresh" e o combo de linhas a ser mostrada no log deve atualizar o Log Viewer quando a opção selecionada mudar
- [x] Alterar tab Connections para Integrations
- [x] Alterar tab NTP & Clock para Clock and NTP
- [x] Incluir maneirade alterar o relógio de forma manual, quando NTP estiver desligado
- [x] Mostrar a lista de Webhooks dos componentes instalados em Webhooks
- [x] Criar comando CLI datetime para exibir/alterar data e hora
- [x] Criar comando CLI webhooks para configurar opçoes de Webhooks
- [x] Criar página Users para gestão de usuários - Página Users fica depois de Setup
- [x] A notificaço pode ficar um pouco mais abaixo da barra superior, coisa de 1cm mais baixo
- [x] Precisamos de uma maneira melhor para tratar reboot no Web UI - ao reiniciar, deve mostrar uma página informando que o DeviceIQ está reinciando e aguardar até que o boot termine. Depois, direciona para o login