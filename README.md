# OpenWrt APK WCX

Плагин чтения пакетов OpenWrt для Total Commander под Windows 10/11. Он показывает дерево файлов, поддерживает просмотр и извлечение обычных файлов, а команда **Test archives** проверяет их SHA-256. Подпись пакета не проверяется. Скрипты пакета не выполняются.

Поддерживаются несжатый ADB и raw DEFLATE (`ADBd` и `ADBc` с алгоритмом 1). Сборки x86 и x64 прошли тесты DLL, включая извлечение и проверку файлов из исходного сжатого пакета OpenWrt 25.12.0. Сборки `APKVIEW_NO_ZLIB` читают только несжатые пакеты.

## Сборка и установочный ZIP

Нужны Visual Studio 2022 Build Tools с C++ toolchain для Win32/x64, Windows SDK и CMake 3.24+. Если `cmake` и `ctest` не в `PATH`, откройте Developer PowerShell for VS 2022 или используйте исполняемые файлы из комплекта Visual Studio.

CMake проверяет SHA-256 исходного архива [zlib 1.4](https://zlib.net/zlib-1.4.tar.gz) и статически компилирует перечисленные в проекте файлы `.c`, не запуская сторонние скрипты сборки. Для уже скачанного архива передайте `-DAPKVIEW_ZLIB_ARCHIVE=C:/path/to/zlib-1.4.tar.gz` в обе команды настройки; проверка SHA-256 остаётся включённой. Для интеграционного теста передайте `-DAPKVIEW_REAL_PACKAGE=C:/path/to/zlib-1.3.1-r1.apk`: CMake проверит контрольную сумму официального тестового пакета и добавит тест `real_openwrt_package`.

```powershell
cmake --preset win-x86
cmake --build --preset win-x86-release
ctest --preset win-x86-release
cmake --preset win-x64
cmake --build --preset win-x64-release
ctest --preset win-x64-release
```

После успешной сборки и тестов обеих разрядностей установите файлы в один новый каталог и упакуйте его содержимое:

```powershell
$stage = "dist/stage-$([guid]::NewGuid().ToString('N'))"
cmake --install build/win-x86 --config Release --prefix $stage
cmake --install build/win-x64 --config Release --prefix $stage
Compress-Archive -Path "$stage/*" -DestinationPath dist/apkview.zip
```

## Локальная проверка без zlib

```powershell
cmake --preset win-x64 -B build/no-zlib-x64 -DAPKVIEW_NO_ZLIB=ON
cmake --build build/no-zlib-x64 --config Release
ctest --test-dir build/no-zlib-x64 -C Release --output-on-failure
```

Для Win32 замените `x64` на `x86`. Тест DLL загружает плагин теми же экспортами, которые использует Total Commander, но не заменяет проверку в его интерфейсе.

## Установка в Total Commander

Откройте `apkview.zip` в Total Commander и подтвердите установку плагина. Если автоустановка отключена, воспользуйтесь ручной установкой.

Распакуйте `apkview.wcx` и `apkview.wcx64` в один каталог. В Total Commander откройте **Configuration → Options → Packer → Configure packer extension WCXs**, задайте расширение `apk` без точки и выберите плагин через **New type**. В архив можно войти двойным щелчком или через Ctrl+PgDn, затем использовать F3 для просмотра и F5 для копирования. Расширение `.apk` общее с Android, но плагин распознаёт только контейнер ADB со схемой пакета `pckg`. Удаление ассоциации отключает плагин; обновляйте оба файла после закрытия Total Commander.

## Ограничения

- Только пакеты APK v3, без Android APK, APK v2, IPK, Zstandard и индексов `packages.adb`.
- Символьные и жёсткие ссылки и специальные файлы видны в списке, но не извлекаются. При распаковке нескольких записей исключите такие файлы из выбора.
- Извлечение поддерживает локальные абсолютные пути с буквой диска без точек повторной обработки (reparse points). Пути с неоднозначными или недопустимыми именами не переписываются автоматически.
- Пределы: 16 МиБ метаданных, 50 000 записей, 8 МиБ индекса имён, 64 компонента в пути и 8 ГиБ распакованного потока. Сжатие относится к потоку целиком, поэтому размер отдельного файла в сжатом виде не показывается.

## Лицензия

Код плагина распространяется по GNU Affero General Public License версии 3 (`AGPL-3.0-only`). Полный текст — в [LICENSE](LICENSE). Библиотека zlib распространяется по [собственной лицензии](https://zlib.net/zlib_license.html); в установочном ZIP она находится в `LICENSE-zlib.txt`.

## Справочные материалы

- [Total Commander Packer Plugin SDK](https://ghisler.github.io/WCX-SDK/table_of_contents.htm)
- [Исходники apk-tools](https://gitlab.alpinelinux.org/alpine/apk-tools)
- [Реальный тестовый пакет OpenWrt 25.12.0](https://downloads.openwrt.org/releases/25.12.0/packages/x86_64/base/zlib-1.3.1-r1.apk)
