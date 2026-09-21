# DisplaySwitch

Sunshine/Moonlight 사용 시 물리 모니터와 가상 모니터(VDD)를 자동으로 전환하는 Windows 프로그램입니다.

## 구성

* `DisplaySelector.exe` - 백그라운드에서 모니터 전환을 담당
* `DisplaySwitch.exe` - 모니터 전환 명령을 보내는 프로그램
* `config.ini` - VDD 설정
* `install.bat` - 설치
* `uninstall.bat` - 제거

## 설치

배포 파일을 같은 폴더에 둡니다.

```text
install.bat
uninstall.bat
DisplaySelector.exe
DisplaySwitch.exe
config.ini
```

`install.bat`을 실행하고 관리자 권한을 승인합니다.

설치 프로그램은:

* `C:\Program Files\DisplaySelector`에 프로그램을 설치합니다.
* Windows 로그인 시 daemon이 자동으로 실행되도록 등록합니다.
* 설치가 끝나면 daemon을 바로 실행합니다.

## 제거

`uninstall.bat`을 실행합니다.

daemon과 자동 실행 설정이 제거됩니다.

## VDD 설정

`config.ini`에서 VDD 이름을 설정합니다.

```ini
[VDD]
Match=VDD
```

Windows에서 VDD 이름이 예를 들어:

```text
VDD by MTT
```

라면 기본 설정인 `Match=VDD`를 그대로 사용할 수 있습니다.

## 사용

가상 모니터로 전환:

```bat
DisplaySwitch.exe virtual
```

물리 모니터로 복원:

```bat
DisplaySwitch.exe physical
```

현재 상태 확인:

```bat
DisplaySwitch.exe status
```

## Sunshine / Moonlight

Sunshine의 세션 시작 시:

```text
DisplaySwitch.exe virtual
```

세션 종료 시:

```text
DisplaySwitch.exe physical
```

을 실행하도록 설정합니다.

daemon이 실행 중이어야 합니다.

## 복구

daemon은 시작할 때 물리 모니터 상태로 복구를 시도합니다.

따라서 Moonlight 세션 중 PC가 갑자기 종료되어 Sunshine의 종료 명령이 실행되지 않은 경우에도 다음 Windows 로그인 시 물리 모니터 복구를 시도합니다.

## 빌드

개발자가 직접 빌드하는 경우:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

빌드 결과:

```text
build\Release\DisplaySelector.exe
build\Release\DisplaySwitch.exe
```
