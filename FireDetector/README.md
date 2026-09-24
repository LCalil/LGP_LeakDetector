Nome do Projeto: Early Fire Detection for Smart Homes

Proteção Inteligente: Sistema ativo de detecção de gás, fumaça e alarme de incêndio para ambientes residenciais e condomínios.

Sobre o Projeto:

Incêndios residenciais evoluem de forma devastadora antes que haja tempo para uma reação manual. Uma chama em ambiente residencial pode dobrar de tamanho a cada 30 segundos, e o fenômeno Flashover (combustão espontânea de gases acumulados no teto) pode ocorrer em menos de 3 minutos. Em condomínios, um incêndio em uma única unidade compromete a segurança e a vida de centenas de vizinhos.

O nosso projeto foi desenvolvido para resolver esse problema, oferecendo monitoramento ativo, continuo e de baixa latência, unindo microcontroladores e acionamentos de segurança para atuar antes que o fogo se alastre.

Principais Funcionalidades

Monitoramento 24/7: Sensores inteligentes de alta precisão com integração para supervisão constante.

Alertas Multicanal: Disparo instantâneo de alarme sonoro local e envio de notificações para os moradores.

Botão de Emergência (E-Stop): Mecanismo fail-safe acionado por interrupção de hardware que interrompe a alimentação e comanda o fechamento imediato da válvula solenoide de gás.

Prevenção Coletiva: Notificação imediata para centrais de administração condominial, permitindo ações rápidas de evacuação.

Hardware e Componentes

Arduino Mega 2560: Placa de desenvolvimento principal baseada no microcontrolador ATmega2560.

Sensor de Chamas / Gás: Calibrado para leitura e detecção precoce de incêndios e vazamentos.

Buzzer Sonoro: Emissão de alerta sonoro local imediato.

Push Button (E-Stop): Interrupção por hardware para desligamento do alarme, bloqueio de gás e notificação de autoridades.

Protoboard e Jumpers: Estruturação do circuito com baixo custo e fácil escalabilidade.

Arquitetura de Segurança (Pilar Central)

O diferencial do sistema está na redundância e rapidez de resposta:

Interrupção por Hardware: O botão de emergência atua sem depender de conectividade Wi-Fi ou processamento de firmware.

Bloqueio Ativo de Gás: Comando para fechar a válvula solenoide instantaneamente no primeiro sinal de emergência.

Redundância em Tempo Real: Garantia de atuação imediata mesmo em caso de falhas críticas ou saturação dos sensores digitais.

Equipe do Projeto

Projeto desenvolvido pelo time Early Fire Detection for Smart Homes:

Ian Esteves

Lucas Calil

Matheus Alvarenga

Licença

Este projeto está sob a licença MIT. Veja o arquivo LICENSE para mais detalhes.
