# OrcaSlicer direct upload

AnglerOS implements the small OctoPrint API subset that OrcaSlicer uses for
connection testing, G-code upload, and upload-and-print. Both devices must be
on the same local network.

## Configure the physical printer

1. In OrcaSlicer, edit the printer preset and open **Physical Printer**.
2. Set **Host Type** to **Octo/Klipper**.
3. In **Hostname, IP or URL**, enter the URL shown on the AnglerOS display,
   such as `http://192.168.1.42`.
4. Leave **API Key / Password** empty. AnglerOS does not currently require an
   API key on the local network.
5. Select **Test**. OrcaSlicer should report that the OctoPrint connection is
   working, then save the physical printer.

After slicing, **Upload** stores the G-code in AnglerOS. **Upload and Print**
stores it and starts the job immediately when the Marlin printer link is
connected and idle. AnglerOS uses flat G-code storage, so any destination
folder selected in OrcaSlicer is ignored and the uploaded file's base name is
used.

The compatibility endpoints are `GET /api/version` and multipart
`POST /api/files/local`. The existing AnglerOS web upload remains available.
