@echo off
rem Sartax OS - tworzy maszyne wirtualna w VirtualBox z dobrymi ustawieniami
set VBM="C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
set NAME=Sartax OS
set ISO=%~dp0build\sartax.iso
set DISK=%USERPROFILE%\VirtualBox VMs\%NAME%\sartax.vdi

%VBM% createvm --name "%NAME%" --ostype Other_64 --register
%VBM% modifyvm "%NAME%" --memory 2048 --vram 64 --graphicscontroller vboxvga --rtcuseutc on --mouse ps2 --nic1 nat --nictype1 82540EM --boot1 dvd --boot2 disk --ioapic on
%VBM% createmedium disk --filename "%DISK%" --size 2048
%VBM% storagectl "%NAME%" --name IDE --add ide --controller PIIX4
%VBM% storageattach "%NAME%" --storagectl IDE --port 0 --device 0 --type hdd --medium "%DISK%"
%VBM% storageattach "%NAME%" --storagectl IDE --port 1 --device 0 --type dvddrive --medium "%ISO%"
echo.
echo Gotowe - uruchom maszyne "%NAME%" w VirtualBox
pause
