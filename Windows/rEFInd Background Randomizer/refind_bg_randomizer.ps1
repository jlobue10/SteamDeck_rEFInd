<# 

rEFInd background randomizer for Windows by ryanrudolf
https://github.com/ryanrudolfoba/

Prerequisites:

Place png images that you wish to use as backgrounds for rEFInd here -
C:\refind\backgrounds

Place this script in a scheduled task to automatically run at startup.

#>

Clear-Host

# set variables for ESP and random background
Set-Variable -Name "ESP" -value (Compare-Object -PassThru (Get-PsDrive [A-Z]).Name ([char[]] 'DEFGHIJKLMNOPQRSTUVWXYZ') | Select-Object -first 1)
Set-Variable -Name "RandomBackground" -value (Get-ChildItem -Path "c:\refind\backgrounds\*.png" | Get-Random -count 1)

# mount ESP partition to the next available drive letter
mountvol $ESP`: /s

# do some error level checking and proceed accordingly
if ($LASTEXITCODE -eq 0) { 
Write-Host "So far so good. ESP partition has been mounted successfully to $ESP."
Write-Host "Proceed to randomize background image and copy it over to $ESP`:\efi\refind\backgrounds."
Copy-Item "$RandomBackground" -Destination "$ESP`:\efi\refind\backgrounds\background.png"
mountvol $ESP`: /d
Write-Host "All done! Background image has been randomized for rEFInd. ESP has been unmounted. Good bye!"
exit}

else {
Write-Host "Error mounting ESP partition. Exiting immediately."
exit}