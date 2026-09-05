# Formato de configuração (`config.json`)

Este documento descreve o formato do arquivo `data/config.json`, lido pelo firmware a partir de `/config.json` no LittleFS na inicialização (`settings::Load()`, [Settings.cpp](../src/core/Settings.cpp)). O formato atual — o que está em uso hoje no repositório — é identificado pelo campo `ComponentSchemaVersion: 1`.

O arquivo é um único objeto JSON com duas partes bem distintas:

- **Seções de sistema** (`Log`, `Network`, `Update`, `General`, `Orchestrator`, `Web Server`, `Webhooks`, `MQTT`, `Telnet`, `Users`): configuração de infraestrutura do dispositivo. Toleram campos ausentes ou inválidos — nesse caso, cada campo assume seu próprio *default* (ver [Defaults e auto-correção](#defaults-e-auto-correção) abaixo).
- **Catálogo de componentes** (`ComponentSchemaVersion` + `Components`): descreve o hardware ligado ao dispositivo (relés, botões, cortinas, termômetros) e a automação declarativa entre eles. Ao contrário das seções de sistema, este catálogo é **validado como um todo**: se algo estiver fora do formato esperado, ou se `ComponentSchemaVersion` não bater com a versão que o firmware entende, a instalação dos componentes falha por inteiro (`settings::InstallComponents()`, [ComponentConfig.cpp](../src/core/ComponentConfig.cpp)) e nenhum componente é criado.

---

## 1. Seções de sistema

Cada seção abaixo é um objeto opcional dentro da raiz do JSON. Se a seção inteira estiver ausente, todos os seus campos recebem o default listado. Se a seção existir mas um campo específico estiver ausente ou for do tipo errado, apenas aquele campo recai no default (`valor | Defaults.X.Y` em ArduinoJson).

### `Log`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `Endpoint` | inteiro (bitmask) | `5` (`0b00000101` = Serial + File) | Bits: `1`=Serial, `2`=Syslog, `4`=File. Combine somando (ex.: `7` = todos). |
| `Level` | inteiro (bitmask) | `255` (todos os níveis) | Bits: `1`=Error, `2`=Warning, `4`=Information, `8`=Debug. |
| `Syslog Server` | string | `"syslog.svr"` | Host do servidor syslog (usado só se o endpoint Syslog estiver ativo). |
| `Syslog Port` | inteiro | `514` | `0` é tratado como inválido e recai no default. |

### `Network`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `DHCP Client` | bool | `true` | |
| `Hostname` | string | `dev-XXXXXX` (gerado a partir do MAC) | Sintaxe RFC 952/1123/1035 (é o que vai no DHCP option 12 via `WiFi.setHostname()`): sequência de labels separados por `.`, cada label normalizado para minúsculas, `[a-z0-9-]`, sem hífen duplicado/nas pontas, máx. 63 chars; labels vazios (`..`) são descartados; nome completo até 255 chars; vazio após normalização vira `"dev"`. |
| `IP Address` | string | `"0.0.0.0"` | IP inválido ou broadcast (`255.255.255.255`) vira `0.0.0.0`. |
| `Gateway` | string | `"0.0.0.0"` | Inválido, broadcast, multicast ou `0.0.0.0` explícito vira `0.0.0.0`. |
| `Netmask` | string | `"255.255.255.0"` | Precisa ser uma máscara contígua válida; senão recai no default. |
| `DNS Servers` | array de 2 strings | `["8.8.8.8", "8.8.4.4"]` | Exatamente 2 posições; entradas inválidas/broadcast/multicast viram `0.0.0.0`. |
| `SSID` | string | `"IOT-2"` | Até 32 caracteres, caracteres de controle removidos. |
| `Passphrase` | string | `"1921682GenesisIOT-2"` | Precisa ser hex de 64 chars (PSK bruta) **ou** ASCII imprimível de 8–63 chars; qualquer outro valor é **descartado (fica vazia)** — não recai no default, pois isso abriria a rede sem senha silenciosamente. |
| `Connection Timeout` | inteiro (s) | `30` | `0` recai no default. |
| `Reconnect Enabled` | bool | `true` | Aceita também a chave legada `"Online Checking"`. |
| `Reconnect Initial Interval` | inteiro (s) | `5` | `0` recai no default. Aceita também a chave legada `"Online Checking Timeout"`. |
| `Reconnect Maximum Interval` | inteiro (s) | `60` | `0` recai no default. |
| `Fallback AP Enabled` | bool | `true` | |
| `Fallback AP SSID` | string | `""` (vazio = usa o hostname) | |
| `Fallback AP Password` | string | `"DeviceIQ-Setup"` | Vazio é aceito (mantém vazio); qualquer outro valor fora de 8–63 chars ASCII imprimíveis recai no **default** (não fica vazio, pois o AP de recuperação precisa continuar acessível). |
| `Fallback AP Retention` | inteiro (s) | `300` | Tempo que o AP de recuperação fica ativo. |

### `Update`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `Manifest URL` | string | `"https://server.dts-network.com:8081/update-dpk.json"` | Precisa começar com `http://`/`https://`, 10–200 chars, só ASCII imprimível; inválida **fica vazia** (atualização fica efetivamente desativada, não volta ao servidor default). |
| `Allow Insecure` | bool | `true` | Permite TLS sem verificação de certificado. |
| `Enable LAN OTA` | bool | `false` | |
| `Password LAN OTA` | string | `""` | 6–64 chars ASCII imprimível; inválida fica vazia. |
| `Check Interval` | inteiro (s) | `3600` | |
| `Auto Reboot` | bool | `true` | Reinicia automaticamente após aplicar uma atualização. |
| `Debug` | bool | `false` | |
| `Check At Startup` | bool | `true` | |

### `General`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `NTP Update` | bool | `true` | |
| `NTP Server` | string | `"pool.ntp.org"` | 3–128 chars, apenas `[a-z0-9.-]`, sem espaços; qualquer valor inválido (ou vazio) recai em `"pool.ntp.org"`. |
| `Time Zone` | inteiro (h) | `-3` | Fixado (`constrain`) entre `-12` e `14`. |
| `Save State Pooling` | inteiro (s) | `20` | Intervalo de gravação de `state.json`. Valores `<= 1` recaem no default. |

### `Orchestrator`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `Assigned` | bool | `false` | Indica se o dispositivo foi atribuído a um orquestrador central. |
| `Server ID` | string | `""` | Precisa ter exatamente 15 chars alfanuméricos (é normalizado para maiúsculas); qualquer outro valor fica vazio. |
| `IP Address` | string | `"0.0.0.0"` | Mesma validação de IP das demais. |
| `Port` | inteiro | `30030` | `0` recai no default. |

### `Web Server`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `Port` | inteiro | `80` | `0` recai no default. |
| `Enabled` | bool | `true` | |
| `Idle Timeout` | inteiro (ms) | `1800000` (30 min) | `0` desativa o timeout (é um valor válido, não recai no default). |
| `Max Sessions` | inteiro | `4` | `0` recai no default. |

### `Webhooks`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `Enabled` | bool | `false` | Forçado para `false` no carregamento se `Token` for inválido/vazio. |
| `Token` | string | `""` | Precisa ter 15–30 chars alfanuméricos; qualquer outro valor fica vazio. |
| `Port` | inteiro | `81` | `0` recai no default. |

### `MQTT`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `Enabled` | bool | `false` | |
| `Broker` | string | `""` | 3–128 chars, apenas `[a-z0-9.-]`, sem espaços; inválido fica vazio. |
| `Port` | inteiro | `1883` | `0` recai no default. |
| `User` | string | `""` | 3–64 chars, `[A-Za-z0-9._-]`; inválido fica vazio. |
| `Password` | string | `""` | 6–64 chars ASCII imprimível; inválido fica vazio. |
| `Discovery Enabled` | bool | `true` | Publica configuração via Home Assistant MQTT Discovery. |
| `Discovery Prefix` | string | `"homeassistant"` | Até 64 chars, `[A-Za-z0-9_-]`; inválido/vazio recai no default. |

### `Telnet`

| Campo | Tipo | Default | Observações |
|---|---|---|---|
| `Enabled` | bool | `true` | |
| `Port` | inteiro | `23` | `0` recai no default. |
| `Idle Timeout` | inteiro (ms) | `60000` (1 min) | `0` desativa o timeout. |
| `Max Sessions` | inteiro | `3` | `0` recai no default. |

### `Users`

Array de contas administrativas do dispositivo:

```json
{ "Username": "admin", "Admin": true, "Salt": [16 bytes], "Hash": [32 bytes] }
```

- `Salt` (16 bytes) e `Hash` (32 bytes, PBKDF2‑SHA256) — nunca a senha em texto puro.
- Se o arquivo não existir, estiver vazio/corrompido, ou a lista de usuários vier vazia, o firmware cria automaticamente duas contas default e as persiste: `admin`/`admin1234` (administrador) e `user`/`user1234` (não administrador). **Troque essas senhas assim que possível** — elas são conhecidas por qualquer pessoa com acesso ao código-fonte.

---

## 2. Catálogo de componentes

```json
"ComponentSchemaVersion": 1,
"Components": {
    "1": {
        "Setup": { "Name": "...", "Class": "...", "Bus": "...", "Address": 0, ... },
        "Properties": { ... },
        "Events": { "EventoX": "log(...)" }
    }
}
```

### `ComponentSchemaVersion`

Identifica a versão do formato do objeto `Components`. O valor atual, entendido pelo firmware, é **`1`** (constante `ComponentSchemaVersion` em [ComponentConfig.cpp](../src/core/ComponentConfig.cpp) e [Settings.cpp](../src/core/Settings.cpp)).

- Na instalação (boot), `settings::InstallComponents()` **exige** que `ComponentSchemaVersion` seja igual à versão suportada pelo firmware. Se divergir (ausente, `0`, ou qualquer outro número), a instalação inteira falha e **nenhum componente é criado** — mesmo que o resto do objeto `Components` seja válido.
- Ao salvar (`settings::Save()`), o firmware sempre grava a versão atual e preserva o catálogo existente como está *apenas* se a versão gravada no arquivo já bater com a versão atual; caso contrário, ele reconstrói `Components` a partir do estado vivo (runtime) do dispositivo.
- Esse campo existe justamente para permitir evoluir o formato de `Setup`/`Properties`/`Events` no futuro sem quebrar silenciosamente dispositivos com um `config.json` mais antigo: uma mudança incompatível de formato deve vir acompanhada de um incremento do número de versão e de lógica de migração (hoje inexistente — uma versão desconhecida simplesmente rejeita a instalação).

### `Components`

Objeto (não array) cujas **chaves são o ID do componente** — um inteiro positivo em string, ex. `"1"`, `"2"` — usado como referência estável em `Events`, em grupos `Blinds` e nos comandos `comp` do CLI. Regras gerais:

- Até **32 componentes** no total (`MaxConfiguredComponents`).
- Cada entrada tem exatamente três seções, todas obrigatórias: `Setup`, `Properties`, `Events` (mesmo que `Properties`/`Events` sejam objetos vazios `{}`).
- `Name` (em `Setup`) deve ser único entre todos os componentes (case-insensitive).
- Para componentes endereçáveis (Relay, Button, Thermometer), o par `(Bus, Address)` deve ser único.
- `Setup.Class` determina qual sub-schema abaixo se aplica.

#### Classe `Relay`

```json
"Setup": { "Name": "GarageLights", "Class": "Relay", "Bus": "Onboard", "Address": 2, "Type": "NormallyOpen", "DriveMode": "ActiveHigh" },
"Properties": { "Enabled": true, "State": false },
"Events": { "Changed": "log(%NAME% changed)" }
```

| Campo | Obrigatório | Default | Valores aceitos |
|---|---|---|---|
| `Setup.Bus` | sim | — | apenas `"Onboard"` |
| `Setup.Address` | sim | — | `0`–`255` |
| `Setup.Type` | não | `"NormallyOpen"` | `"NormallyOpen"` \| `"NormallyClosed"` |
| `Setup.DriveMode` | não | `"ActiveHigh"` | `"ActiveHigh"` \| `"ActiveLow"` |
| `Properties.Enabled` | não | `true` | bool |
| `Properties.State` | não | `false` | bool — apenas a **semente** inicial; se `state.json` já tiver um valor persistido para este ID, ele prevalece. |
| `Events` | — | — | `SettingOn`, `SettingOff`, `SetOn`, `SetOff`, `Changed`, `WriteFailed` |

#### Classe `Button`

```json
"Setup": { "Name": "WallButton", "Class": "Button", "Bus": "Onboard", "Address": 0, "ActiveLevel": "Low", "InputMode": "PullUp", "DebounceTimeMs": 50, "LongClickTimeMs": 1000, "MultiClickTimeMs": 400 },
"Properties": { "Enabled": true },
"Events": { "Clicked": "compset(GarageLights state=toggle)" }
```

| Campo | Obrigatório | Default | Valores aceitos |
|---|---|---|---|
| `Setup.Bus` | sim | — | apenas `"Onboard"` |
| `Setup.Address` | sim | — | `0`–`255` |
| `Setup.ActiveLevel` | não | `"Low"` | `"High"` \| `"Low"` |
| `Setup.InputMode` | não | `"PullUp"` | `"Floating"` \| `"PullUp"` \| `"PullDown"` |
| `Setup.DebounceTimeMs` | não | `50` | inteiro (ms) |
| `Setup.LongClickTimeMs` | não | `1000` | inteiro (ms) |
| `Setup.MultiClickTimeMs` | não | `400` | inteiro (ms) |
| `Properties.Enabled` | não | `true` | bool |
| `Events` | — | — | `Pressed`, `Released`, `Clicked`, `LongClicked`, `DoubleClicked`, `TripleClicked`, `Changed` |

#### Classe `Thermometer`

```json
"Setup": { "Name": "RoomClimate", "Class": "Thermometer", "Bus": "Onboard", "Address": 15, "Type": "DHT22", "PollingIntervalMs": 5000 },
"Properties": { "Enabled": true },
"Events": { "TemperatureChanged": "log(%NAME% temperature changed)" }
```

| Campo | Obrigatório | Default | Valores aceitos |
|---|---|---|---|
| `Setup.Bus` | sim | — | `"Onboard"` \| `"I2C"` |
| `Setup.Address` | sim | — | `0`–`255`; se `Bus` = `"I2C"`, precisa ser `1`–`127` (`0x7F`) |
| `Setup.Type` | não | `"Ds18b20"` | `"Dht11"`, `"Dht12"`, `"Dht21"`, `"Dht22"`, `"Ds18b20"` (case-insensitive). Se `Bus` = `"I2C"`, **só** `"Dht12"` é aceito. |
| `Setup.PollingIntervalMs` | não | `5000` | inteiro (ms), mínimo `1000` |
| `Properties.Enabled` | não | `true` | bool |
| `Events` | — | — | `TemperatureChanged`, `HumidityChanged`, `Changed`, `ReadFailed`, `ReadRecovered` |

#### Classe `Blinds`

Um grupo lógico que combina 2 relés (obrigatórios) e até 2 botões (opcionais), referenciados pelo **ID** de outros componentes já definidos em `Components`.

```json
"Setup": {
    "Name": "BedroomBlinds", "Class": "Blinds", "Bus": "Group",
    "RelayUp": 3, "RelayDown": 4, "ButtonUp": 5, "ButtonDown": 6,
    "OpenStepTimeMs": 280, "CloseStepTimeMs": 240,
    "OpenCorrectionFactor": 0.35, "CloseCorrectionFactor": 0.20,
    "EndstopMarginMs": 2000, "ReversalDelayMs": 250
},
"Properties": { "Enabled": true, "Position": 0 },
"Events": { "Opened": "log(%NAME% opened)" }
```

| Campo | Obrigatório | Default | Valores aceitos |
|---|---|---|---|
| `Setup.Bus` | sim | — | apenas `"Group"` |
| `Setup.RelayUp` / `Setup.RelayDown` | sim | — | ID de um componente `Relay` já configurado; devem ser diferentes entre si |
| `Setup.ButtonUp` / `Setup.ButtonDown` | não | nenhum | ID de um componente `Button`; se presentes, devem ser diferentes entre si |
| `Setup.OpenStepTimeMs` / `Setup.CloseStepTimeMs` | **sim, para o componente ser instalado** | `250` (constante, mas na prática exigido) | inteiro (ms) > 0. Sem esses dois campos válidos, o grupo é **ignorado silenciosamente** (aviso no log), não trava a instalação dos demais componentes. |
| `Setup.OpenCorrectionFactor` / `Setup.CloseCorrectionFactor` | não | `0.0` | float `0.0`–`0.95` |
| `Setup.EndstopMarginMs` | não | `0` | inteiro (ms) |
| `Setup.ReversalDelayMs` | não | `250` | inteiro (ms) |
| `Properties.Enabled` | não | `true` | bool |
| `Properties.Position` | não | `0` | inteiro `0`–`100` (%) — semente inicial; sobrescrita por `state.json` se presente |
| `Events` | — | — | `Changed`, `Opening`, `Closing`, `Stopped`, `Opened`, `Closed`, `Fault` |

Os relés e botões referenciados por um `Blinds` tornam-se **membros privados** do grupo: continuam existindo como componentes próprios (com seu ID), mas deixam de ter automação/MQTT independentes — apenas o `Blinds` publica eventos e aceita comandos externamente.

### `Events`: automação declarativa

Cada valor de `Events` é uma *script string* com uma das duas ações suportadas (`automation::ParseScript`, [Automation.cpp](../src/core/Automation.cpp)):

- `log(texto)` — grava `texto` no log do dispositivo.
- `compset(seletor propriedade=valor)` — aplica `propriedade=valor` em outro componente. `seletor` pode ser `#<id>` (ex. `#3`) ou o `Name` do componente.

Em ambos os casos, a substring `%NAME%` é trocada pelo `Name` do componente que disparou o evento — em qualquer parte do script (texto do log, seletor, propriedade ou valor).

---

## 3. Defaults e auto-correção

O firmware nunca falha ao carregar por causa de um campo de sistema ausente ou inválido — ele sempre recai em um valor seguro. Os defaults concretos vivem centralizados em [`Defaults.h`](../src/core/Defaults.h) (`struct defaults`, instância global `Defaults`), e são aplicados em duas situações:

1. **Arquivo ausente ou ilegível** (primeiro boot, JSON corrompido, ou raiz que não é um objeto): `settings::LoadDefaults()` roda por completo, preenchendo todas as seções de sistema com os valores de `Defaults.*`, e as contas `admin`/`user` default são criadas.
2. **Campo individual ausente ou do tipo errado dentro de uma seção existente**: o operador `|` do ArduinoJson (`json["Campo"] | Defaults.Secao.Campo`) faz o valor cair no default daquele campo especificamente, sem afetar os demais.

Além disso, cada *setter* de `settings` (em [Settings.cpp](../src/core/Settings.cpp)) aplica sua própria sanitização/validação sobre o valor já lido — é aqui que um valor presente, mas **semanticamente inválido**, é tratado. O comportamento não é uniforme, e a distinção importa para quem provisiona um `config.json` manualmente:

- **Recai no default** (`Defaults.*`): portas (`0` → porta default), contagens/limites (`Max Sessions`, `Save State Pooling`, `Connection Timeout`, intervalos de reconexão), `NTP Server`, `Discovery Prefix`, `Netmask`, `Fallback AP Password` (quando não vazio e inválido).
- **Fica vazio/desativado**, em vez de usar o default — para não mascarar silenciosamente uma credencial ou endpoint mal configurado: `Passphrase` do Wi-Fi, `Manifest URL` de atualização, `Password LAN OTA`, `Server ID` do orquestrador, `Token` de Webhooks (o que também força `Webhooks.Enabled = false`), `Broker`/`User`/`Password` do MQTT.
- **`Idle Timeout` (Web/Telnet)**: `0` é um valor válido (desativa o timeout), não recai em default.

O catálogo de componentes (`Components`) não segue esse modelo de "cada campo com seu default": campos opcionais de `Setup`/`Properties` têm defaults pontuais (documentados nas tabelas da seção 2), mas a estrutura como um todo — chaves válidas, `ComponentSchemaVersion`, unicidade de nome/endereço, referências de `Blinds` — é validada atomicamente. Um erro em qualquer componente aborta a instalação de todos.

### Outros defaults do firmware

Não fazem parte do JSON, mas valem citar por completude (também em `Defaults.h`):

| Constante | Valor | Uso |
|---|---|---|
| `ConfigFileName` | `/config.json` | Caminho padrão lido/gravado por `Load()`/`Save()`. |
| `StateFileName` | `/state.json` | Estado de runtime dos componentes (ex.: `Relay.State`, `Blinds.Position`), salvo a cada `Save State Pooling` segundos. |
| `LogFileName` | `/device.log` | Usado quando o endpoint `File` está ativo em `Log.Endpoint`. |
| `Users.Admin` | `admin` / `admin1234` | Conta administrativa criada no primeiro boot. |
| `Users.User` | `user` / `user1234` | Conta não administrativa criada no primeiro boot. |
