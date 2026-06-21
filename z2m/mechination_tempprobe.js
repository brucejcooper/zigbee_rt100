// Zigbee2MQTT 2.x external converter for the Mechination "TempProbe-C6-RTD".
//
// XIAO ESP32-C6 + Adafruit MAX31865 PT100 probe. Exposes temperature, an RTD
// fault flag, the live antenna selection (writable), and read-only diagnostics
// describing the sensor configuration.
//
// Install: copy this file into the Zigbee2MQTT `external_converters/` folder
// (next to configuration.yaml, i.e. <data>/external_converters/), then restart
// Zigbee2MQTT. The device's "Automatically generated definition" badge should
// disappear and it should show as Mechination TempProbe-C6-RTD.

import {Zcl} from 'zigbee-herdsman';
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['TempProbe-C6-RTD'],
    model: 'TempProbe-C6-RTD',
    vendor: 'Mechination',
    description: 'PT100 RTD temperature probe (ESP32-C6 + MAX31865) with fault reporting',
    // Device icon (Mechination logo) embedded so it ships with the converter.
    icon: 'data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+CjwhLS0gQ3JlYXRlZCB3aXRoIElua3NjYXBlIChodHRwOi8vd3d3Lmlua3NjYXBlLm9yZy8pIC0tPgoKPHN2ZwogICB3aWR0aD0iMTI4IgogICBoZWlnaHQ9IjEyOCIKICAgdmlld0JveD0iMCAwIDMzLjg2NjY2NiAzMy44NjY2NjciCiAgIHZlcnNpb249IjEuMSIKICAgaWQ9InN2ZzUiCiAgIGlua3NjYXBlOnZlcnNpb249IjEuMS4yICgwYTAwY2Y1MzM5LCAyMDIyLTAyLTA0KSIKICAgc29kaXBvZGk6ZG9jbmFtZT0ibG9nby1zcXVhcmUuc3ZnIgogICBpbmtzY2FwZTpleHBvcnQtZmlsZW5hbWU9Ii9ydW4vdXNlci8xMDAwL2d2ZnMvc21iLXNoYXJlOnNlcnZlcj1uYXMuOGJpdGNsb3VkLmNvbSxzaGFyZT1ob21lL0dEcml2ZS9NZWNoaW5hdGlvbi9CcmFuZGluZy9sb2dvLXNxdWFyZS5wbmciCiAgIGlua3NjYXBlOmV4cG9ydC14ZHBpPSIzMDAuMzE3NjYiCiAgIGlua3NjYXBlOmV4cG9ydC15ZHBpPSIzMDAuMzE3NjYiCiAgIHhtbG5zOmlua3NjYXBlPSJodHRwOi8vd3d3Lmlua3NjYXBlLm9yZy9uYW1lc3BhY2VzL2lua3NjYXBlIgogICB4bWxuczpzb2RpcG9kaT0iaHR0cDovL3NvZGlwb2RpLnNvdXJjZWZvcmdlLm5ldC9EVEQvc29kaXBvZGktMC5kdGQiCiAgIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIKICAgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+CiAgPHNvZGlwb2RpOm5hbWVkdmlldwogICAgIGlkPSJuYW1lZHZpZXc3IgogICAgIHBhZ2Vjb2xvcj0iI2ZmZmZmZiIKICAgICBib3JkZXJjb2xvcj0iIzY2NjY2NiIKICAgICBib3JkZXJvcGFjaXR5PSIxLjAiCiAgICAgaW5rc2NhcGU6cGFnZXNoYWRvdz0iMiIKICAgICBpbmtzY2FwZTpwYWdlb3BhY2l0eT0iMC4wIgogICAgIGlua3NjYXBlOnBhZ2VjaGVja2VyYm9hcmQ9IjAiCiAgICAgaW5rc2NhcGU6ZG9jdW1lbnQtdW5pdHM9InB4IgogICAgIHNob3dncmlkPSJmYWxzZSIKICAgICBpbmtzY2FwZTp6b29tPSI0LjM0IgogICAgIGlua3NjYXBlOmN4PSItMTIuMzI3MTg5IgogICAgIGlua3NjYXBlOmN5PSIxMzcuMzI3MTkiCiAgICAgaW5rc2NhcGU6d2luZG93LXdpZHRoPSIyNDQ4IgogICAgIGlua3NjYXBlOndpbmRvdy1oZWlnaHQ9IjE2MDUiCiAgICAgaW5rc2NhcGU6d2luZG93LXg9IjI2IgogICAgIGlua3NjYXBlOndpbmRvdy15PSIyMyIKICAgICBpbmtzY2FwZTp3aW5kb3ctbWF4aW1pemVkPSIwIgogICAgIGlua3NjYXBlOmN1cnJlbnQtbGF5ZXI9ImxheWVyMSIKICAgICB1bml0cz0icHgiCiAgICAgaW5rc2NhcGU6c2hvd3BhZ2VzaGFkb3c9IjIiCiAgICAgaW5rc2NhcGU6ZGVza2NvbG9yPSIjZDFkMWQxIiAvPgogIDxkZWZzCiAgICAgaWQ9ImRlZnMyIiAvPgogIDxnCiAgICAgaW5rc2NhcGU6bGFiZWw9IkxheWVyIDEiCiAgICAgaW5rc2NhcGU6Z3JvdXBtb2RlPSJsYXllciIKICAgICBpZD0ibGF5ZXIxIgogICAgIHRyYW5zZm9ybT0idHJhbnNsYXRlKC0yMy4yNzYyMywtMTI3LjEyOTA0KSI+CiAgICA8cmVjdAogICAgICAgc3R5bGU9ImZpbGw6IzBhMzA0MjtmaWxsLW9wYWNpdHk6MTtzdHJva2Utd2lkdGg6MC4yNjQ1ODMiCiAgICAgICBpZD0icmVjdDk0NiIKICAgICAgIHdpZHRoPSIzMy44NjY2NjUiCiAgICAgICBoZWlnaHQ9IjMzLjg2NjY2NSIKICAgICAgIHg9IjIzLjI3NjIzIgogICAgICAgeT0iMTI3LjEyOTA0IiAvPgogICAgPGcKICAgICAgIGlkPSJTdmdqc0cxNTk2IgogICAgICAgZmVhdHVyZWtleT0iUU11c2kxLTAiCiAgICAgICB0cmFuc2Zvcm09Im1hdHJpeCgwLjAzOTk4MDU0LDAsMCwtMC4wMzk2ODkyMSwyMC4yMTg3OCwxNjMuOTA0OTkpIgogICAgICAgZmlsbD0iIzY0ZWU4NSIKICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiPgogICAgICA8ZwogICAgICAgICBpZD0iZzQ2IgogICAgICAgICBzdHlsZT0iZmlsbDojMDBmZmZmO2ZpbGwtb3BhY2l0eToxIj4KICAgICAgICA8ZwogICAgICAgICAgIGlkPSJnMTIiCiAgICAgICAgICAgc3R5bGU9ImZpbGw6IzAwZmZmZjtmaWxsLW9wYWNpdHk6MSI+CiAgICAgICAgICA8cGF0aAogICAgICAgICAgICAgZD0ibSA2NjkuNiwxNjQuNCBjIDE4LjYsLTQuNiAzMC40LDYuMSA0MC40LDI1LjEgMTYuOSwzMi4zIDM2LjEsNjMuNSA1NC45LDk0LjkgNywxMS42IDcuOCwyMS42IDAuNiwzMy44IC0zNiw2MS4yIC03MS40LDEyMi42IC0xMDYuNiwxODQuMiAtNy45LDEzLjkgLTExLjgsMTMgLTIxLjcsMC4zIC0yMS42LC0yNy43IC0yMC42LC01MS41IC0wLjQsLTgwLjUgMjMuNywtMzQgNDEuOCwtNzEuOCA2My4xLC0xMDcuNSA2LC0xMC4xIDUuNCwtMTcuOCAtMC40LC0yNy41IC0xOS41LC0zMi42IC0zOC41LC02NS41IC01Ny4xLC05OC42IC0xMi41LC0yMi4zIC0xMS4zLC0yMy45IDE1LjIsLTI0LjEgMi43LC0wLjEgNS42LC0wLjEgMTIsLTAuMSB6IgogICAgICAgICAgICAgaWQ9InBhdGgxMCIKICAgICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiIC8+CiAgICAgICAgPC9nPgogICAgICAgIDxnCiAgICAgICAgICAgaWQ9ImcxNiIKICAgICAgICAgICBzdHlsZT0iZmlsbDojMDBmZmZmO2ZpbGwtb3BhY2l0eToxIj4KICAgICAgICAgIDxwYXRoCiAgICAgICAgICAgICBkPSJtIDg3NS4yLDQ3Ny42IGMgMTMuMywxMy43IDEwLDI5LjQgLTEuNCw0Ny41IC0xOS40LDMwLjkgLTM2LjcsNjMuMiAtNTQuMyw5NS4yIC02LjUsMTEuOCAtMTQuOCwxNy42IC0yOC45LDE3LjYgLTcwLjksLTAuMyAtMTQxLjksMCAtMjEyLjgsMC42IC0xNiwwLjEgLTE3LjIsLTMuNiAtMTEuMiwtMTguNyAxMy4xLC0zMi42IDM0LjIsLTQzLjcgNjkuMywtNDAuOSA0MS4zLDMuNCA4My4xLC0wLjEgMTI0LjYsMC40IDExLjcsMC4xIDE4LjEsLTQuMiAyMy42LC0xNC4yIDE4LjMsLTMzLjMgMzcuMiwtNjYuMiA1Ni41LC05OSAxMywtMjIuMSAxNSwtMjEuOCAyOC41LDEgMS41LDIuNSAyLjksNC45IDYuMSwxMC41IHoiCiAgICAgICAgICAgICBpZD0icGF0aDE0IgogICAgICAgICAgICAgc3R5bGU9ImZpbGw6IzAwZmZmZjtmaWxsLW9wYWNpdHk6MSIgLz4KICAgICAgICA8L2c+CiAgICAgICAgPGcKICAgICAgICAgICBpZD0iZzIwIgogICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiPgogICAgICAgICAgPHBhdGgKICAgICAgICAgICAgIGQ9Im0gNzA4LjEsODEyLjggYyAtNS4xLDE4LjQgLTIwLjQsMjMuNSAtNDEuNywyMi43IC0zNi41LC0xLjIgLTczLjEsMC4xIC0xMDkuNiwxIC0xMy41LDAuMyAtMjIuNiwtMy45IC0yOS43LC0xNi4xIEMgNDkxLjYsNzU5IDQ1NS42LDY5Ny44IDQxOS40LDYzNi44IGMgLTguMiwtMTMuOCAtNS41LC0xNi43IDEwLjUsLTE5IDM0LjgsLTUuMSA1NSw3LjUgNzAuMiwzOS4zIDE3LjksMzcuNCA0MS45LDcxLjggNjIuNCwxMDcuOSA1LjgsMTAuMiAxMi44LDEzLjUgMjQuMiwxMy4yIDM4LC0wLjkgNzYsLTEuMiAxMTMuOSwtMSAyNS42LDAuMSAyNi4zLDIgMTMuNSwyNS4xIC0xLjQsMi41IC0yLjgsNSAtNiwxMC41IHoiCiAgICAgICAgICAgICBpZD0icGF0aDE4IgogICAgICAgICAgICAgc3R5bGU9ImZpbGw6IzAwZmZmZjtmaWxsLW9wYWNpdHk6MSIgLz4KICAgICAgICA8L2c+CiAgICAgICAgPGcKICAgICAgICAgICBpZD0iZzI0IgogICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiPgogICAgICAgICAgPHBhdGgKICAgICAgICAgICAgIGQ9Im0gMzM0LjQsODM3LjMgYyAtMTguNSw0LjggLTMwLjUsLTUuOCAtNDAuNiwtMjQuNiAtMTcuMywtMzIuMSAtMzYuOSwtNjMuMSAtNTYsLTk0LjIgLTcuMSwtMTEuNSAtOCwtMjEuNSAtMSwtMzMuOCAzNS4yLC02MS42IDY5LjksLTEyMy41IDEwNC40LC0xODUuNSA3LjgsLTE0IDExLjYsLTEzLjIgMjEuNywtMC41IDIxLjksMjcuNSAyMS4yLDUxLjMgMS40LDgwLjUgLTIzLjMsMzQuMyAtNDAuOSw3Mi4zIC02MS44LDEwOC4yIC01LjksMTAuMiAtNS4yLDE3LjkgMC43LDI3LjUgMTkuOSwzMi4zIDM5LjIsNjUgNTguMyw5Ny45IDEyLjgsMjIuMiAxMS42LDIzLjggLTE0LjksMjQuMyAtMywwLjEgLTUuOSwwLjEgLTEyLjIsMC4yIHoiCiAgICAgICAgICAgICBpZD0icGF0aDIyIgogICAgICAgICAgICAgc3R5bGU9ImZpbGw6IzAwZmZmZjtmaWxsLW9wYWNpdHk6MSIgLz4KICAgICAgICA8L2c+CiAgICAgICAgPGcKICAgICAgICAgICBpZD0iZzI4IgogICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiPgogICAgICAgICAgPHBhdGgKICAgICAgICAgICAgIGQ9Im0gMTI1LjEsNTI2LjYgYyAtMTMuNSwtMTMuNiAtMTAuNCwtMjkuMyAwLjgsLTQ3LjUgMTkuMSwtMzEuMSAzNiwtNjMuNiA1My4yLC05NS45IDYuNCwtMTEuOSAxNC41LC0xNy44IDI4LjcsLTE3LjkgNzAuOSwtMC41IDE0MS45LC0xLjcgMjEyLjgsLTMuMSAxNiwtMC4zIDE3LjIsMy40IDExLjQsMTguNSAtMTIuNywzMi44IC0zMy42LDQ0LjEgLTY4LjgsNDEuNyAtNDEuMywtMi45IC04My4xLDEgLTEyNC42LDEuMSAtMTEuNywwIC0xOC4xLDQuNSAtMjMuNCwxNC41IC0xNy45LDMzLjUgLTM2LjQsNjYuNyAtNTUuMyw5OS42IC0xMi43LDIyLjIgLTE0LjcsMjEuOSAtMjguNSwtMC42IC0xLjYsLTIuNCAtMy4xLC00LjkgLTYuMywtMTAuNCB6IgogICAgICAgICAgICAgaWQ9InBhdGgyNiIKICAgICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiIC8+CiAgICAgICAgPC9nPgogICAgICAgIDxnCiAgICAgICAgICAgaWQ9ImczMiIKICAgICAgICAgICBzdHlsZT0iZmlsbDojMDBmZmZmO2ZpbGwtb3BhY2l0eToxIj4KICAgICAgICAgIDxwYXRoCiAgICAgICAgICAgICBkPSJtIDI4OC4xLDE4OS40IGMgNC45LC0xOC41IDIwLjEsLTIzLjcgNDEuNCwtMjMuMiAzNi41LDAuOCA3My4xLC0xIDEwOS42LC0yLjMgMTMuNSwtMC41IDIyLjcsMy42IDI5LjksMTUuOCAzNi4yLDYxIDcyLjksMTIxLjcgMTA5LjksMTgyLjMgOC4zLDEzLjcgNS43LDE2LjYgLTEwLjMsMTkuMiAtMzQuNyw1LjUgLTU1LjEsLTYuOSAtNzAuNywtMzguNSAtMTguMywtMzcuMSAtNDIuNywtNzEuMiAtNjMuNywtMTA3LjEgLTUuOSwtMTAuMSAtMTIuOSwtMTMuNCAtMjQuMywtMTMgLTM4LDEuNCAtNzUuOSwyLjEgLTExMy45LDIuNCAtMjUuNiwwLjIgLTI2LjQsLTEuNyAtMTMuOCwtMjQuOSAxLjQsLTIuNiAyLjgsLTUuMSA1LjksLTEwLjcgeiIKICAgICAgICAgICAgIGlkPSJwYXRoMzAiCiAgICAgICAgICAgICBzdHlsZT0iZmlsbDojMDBmZmZmO2ZpbGwtb3BhY2l0eToxIiAvPgogICAgICAgIDwvZz4KICAgICAgICA8cGF0aAogICAgICAgICAgIGQ9Im0gNDA2LjQsMjczIGMgLTQxLjIsMC4yIC04Mi40LDEuOCAtMTIzLjYsMS45IC05LjIsLTE1LjcgLTI2LjQsLTI2LjMgLTQ1LjksLTI2LjMgLTI5LjQsMCAtNTMuMywyMy45IC01My4zLDUzLjMgMCwyOS40IDIzLjksNTMuMyA1My4zLDUzLjMgMTguNCwwIDM0LjYsLTkuMyA0NC4yLC0yMy41IDI4LC0xLjEgNTYuMSwtMi41IDgzLjksLTAuNCAyOS4zLDIuMiA0Mi44LC0xMi41IDU0LjcsLTM0LjIgMTAuNSwtMTkuNSA4LjUsLTI0LjIgLTEzLjMsLTI0LjEgeiBtIC0xNjkuNSw1Ni44IGMgLTIuNiwwIC01LjIsLTAuNCAtNy42LC0xIC0xMS43LC0zLjMgLTIwLjMsLTE0LjEgLTIwLjMsLTI2LjkgMCwtMTUuNCAxMi41LC0yNy45IDI3LjksLTI3LjkgNC42LDAgOSwxLjEgMTIuOSwzLjEgOC45LDQuNyAxNSwxNCAxNSwyNC44IDAsMTUuNCAtMTIuNSwyNy45IC0yNy45LDI3LjkgeiIKICAgICAgICAgICBpZD0icGF0aDM0IgogICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiIC8+CiAgICAgICAgPHBhdGgKICAgICAgICAgICBkPSJtIDI4NC4zLDQ2Ny4yIGMgLTExLjQsLTE4LjkgLTE2LjYsLTE5LjYgLTI3LjQsLTAuNyAtMjIuMiwzOC41IC00Mi45LDc3LjggLTY1LjQsMTE2LjEgLTAuNCwwLjcgLTAuOCwxLjQgLTEuMiwyIC0yOC42LDAuOCAtNTEuNywyNC40IC01MS43LDUzLjIgMCwyOS40IDIzLjksNTMuMyA1My4zLDUzLjMgMjkuNCwwIDUzLjMsLTIzLjkgNTMuMywtNTMuMyAwLC04LjYgLTIuMSwtMTYuOCAtNS43LC0yNCAxNSwtMjcuOCAyOS4xLC01Ni4yIDQ3LC04Mi4xIDE2LjUsLTI0LjIgMTAuNSwtNDMuMyAtMi4yLC02NC41IHogbSAtNjYsMTc5LjQgYyAtMy43LDExLjEgLTE0LjIsMTkuMiAtMjYuNSwxOS4yIC0xNS40LDAgLTI3LjksLTEyLjUgLTI3LjksLTI3LjkgMCwtMTEuNSA3LC0yMS41IDE3LC0yNS43IDMuMywtMS40IDcsLTIuMiAxMC45LC0yLjIgMTUuNCwwIDI3LjksMTIuNSAyNy45LDI3LjkgMCwzIC0wLjUsNS45IC0xLjQsOC43IHoiCiAgICAgICAgICAgaWQ9InBhdGgzNiIKICAgICAgICAgICBzdHlsZT0iZmlsbDojMDBmZmZmO2ZpbGwtb3BhY2l0eToxIiAvPgogICAgICAgIDxwYXRoCiAgICAgICAgICAgZD0ibSA2NTMuNSwzMDguNCBjIC0yMC43LC0zNy4yIC00Mi44LC03My42IC02My4zLC0xMTAuOSA2LjMsLTguNyA5LjksLTE5LjQgOS45LC0zMC45IDAsLTI5LjQgLTIzLjksLTUzLjMgLTUzLjMsLTUzLjMgLTI5LjQsMCAtNTMuMywyMy45IC01My4zLDUzLjMgMCwyNS45IDE4LjYsNDcuNiA0My4yLDUyLjMgMTYsMjYuOCAzMi44LDUzLjEgNDUuNiw4MS4zIDEyLjIsMjYuOCAzMS42LDMxLjUgNTYuNCwzMS40IDIyLjIsLTAuMSAyNS40LC00LjIgMTQuOCwtMjMuMiB6IE0gNTc0LDE3MyBjIC0yLjksMTIuMyAtMTQsMjEuNSAtMjcuMiwyMS41IC0xMS45LDAgLTIyLjIsLTcuNiAtMjYuMSwtMTguMSAtMS4xLC0zIC0xLjgsLTYuMyAtMS44LC05LjggMCwtMTUuNCAxMi41LC0yNy45IDI3LjksLTI3LjkgMTUuNCwwIDI3LjksMTIuNSAyNy45LDI3LjkgMC4xLDIuMiAtMC4yLDQuMyAtMC43LDYuNCB6IgogICAgICAgICAgIGlkPSJwYXRoMzgiCiAgICAgICAgICAgc3R5bGU9ImZpbGw6IzAwZmZmZjtmaWxsLW9wYWNpdHk6MSIgLz4KICAgICAgICA8cGF0aAogICAgICAgICAgIGQ9Im0gODEyLjksMzE3LjQgYyAtMjkuNCwwIC01My4zLDIzLjkgLTUzLjMsNTMuMyAwLDYuMSAxLDExLjkgMi45LDE3LjMgdiAwIGMgLTE1LjYsMjggLTMwLjIsNTYuNiAtNDguNSw4Mi42IC0xNywyNCAtMTEuMyw0My4yIDEuMiw2NC41IDExLjIsMTkuMSAxNi40LDE5LjggMjcuNCwxLjEgMjIuMSwtMzcuMyA0Mi44LC03NS40IDY1LjEsLTExMi41IDEuNywwLjIgMy40LDAuMiA1LjEsMC4yIDI5LjQsMCA1My4zLC0yMy45IDUzLjMsLTUzLjMgMCwtMjkuNCAtMjMuOCwtNTMuMiAtNTMuMiwtNTMuMiB6IG0gNy4xLDgwLjIgYyAtMi4zLDAuNiAtNC42LDAuOSAtNy4xLDAuOSAtMTUuNCwwIC0yNy45LC0xMi41IC0yNy45LC0yNy45IDAsLTUuOCAxLjgsLTExLjIgNC45LC0xNS43IDUsLTcuNCAxMy41LC0xMi4yIDIzLC0xMi4yIDE1LjQsMCAyNy45LDEyLjUgMjcuOSwyNy45IDAsMTMgLTguOCwyMy45IC0yMC44LDI3IHoiCiAgICAgICAgICAgaWQ9InBhdGg0MCIKICAgICAgICAgICBzdHlsZT0iZmlsbDojMDBmZmZmO2ZpbGwtb3BhY2l0eToxIiAvPgogICAgICAgIDxwYXRoCiAgICAgICAgICAgZD0ibSA3NjkuNCw2NDkuNyBjIC0xNy4xLDAgLTMyLjQsOC4yIC00Mi4yLDIwLjggLTMxLjUsMC43IC02MywyLjQgLTk0LjIsLTAuNCAtMjkuMywtMi42IC00MywxMiAtNTUuMSwzMy41IC0xMC44LDE5LjMgLTguOSwyNC4xIDEyLjksMjQuMiA0My44LDAuMyA4Ny42LC0xIDEzMS40LC0wLjQgOC45LDE3IDI2LjcsMjguNyA0Ny4yLDI4LjcgMjkuNCwwIDUzLjMsLTIzLjkgNTMuMywtNTMuMyAwLC0yOS4yIC0yMy45LC01My4xIC01My4zLC01My4xIHogbSAwLDgxLjEgYyAtNi45LDAgLTEzLjIsLTIuNSAtMTguMSwtNi43IC02LC01LjEgLTkuOCwtMTIuNyAtOS44LC0yMS4yIDAsLTE1LjQgMTIuNSwtMjcuOSAyNy45LC0yNy45IGggMC4xIGMgMTUuNCwwIDI3LjksMTIuNSAyNy45LDI3LjkgLTAuMSwxNS40IC0xMi42LDI3LjkgLTI4LDI3LjkgeiIKICAgICAgICAgICBpZD0icGF0aDQyIgogICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiIC8+CiAgICAgICAgPHBhdGgKICAgICAgICAgICBkPSJtIDQ2Ni4zLDc4MC45IGMgLTE2LjIsLTI2LjQgLTMzLjMsLTUyLjUgLTQ2LjQsLTgwLjQgLTEyLjUsLTI2LjYgLTMyLC0zMS4xIC01Ni43LC0zMC43IC0yMi4xLDAuMyAtMjUuMyw0LjUgLTE0LjUsMjMuMyAyMS4xLDM2LjkgNDMuNiw3MyA2NC41LDEwOS45IC02LDguNiAtOS41LDE5LjEgLTkuNSwzMC4zIDAsMjkuNCAyMy45LDUzLjMgNTMuMyw1My4zIDI5LjQsMCA1My4zLC0yMy45IDUzLjMsLTUzLjMgMCwtMjYuMSAtMTkuMSwtNDcuOSAtNDQsLTUyLjQgeiBtIC05LjMsODAuNCBjIC0xNS40LDAgLTI3LjksLTEyLjUgLTI3LjksLTI3LjkgMCwtMiAwLjIsLTQgMC42LC01LjkgMi43LC0xMi42IDEzLjksLTIyLjEgMjcuMywtMjIuMSAxMS45LDAgMjIuMSw3LjUgMjYuMSwxOCB2IDAgYyAxLjEsMy4xIDEuOCw2LjQgMS44LDkuOSAwLDE1LjUgLTEyLjUsMjggLTI3LjksMjggeiIKICAgICAgICAgICBpZD0icGF0aDQ0IgogICAgICAgICAgIHN0eWxlPSJmaWxsOiMwMGZmZmY7ZmlsbC1vcGFjaXR5OjEiIC8+CiAgICAgIDwvZz4KICAgIDwvZz4KICA8L2c+Cjwvc3ZnPgo=',
    extend: [
        // Diagnostics/control cluster 0xFC00. The firmware registers these as
        // plain (non-manufacturer-specific) custom attributes, so the cluster is
        // advertised in the simple descriptor and z2m frames reads/writes plain.
        // Do NOT add a manufacturerCode here — it must match the firmware, which
        // uses esp_zb_custom_cluster_add_custom_attr (no manufacturer code).
        // Each attribute MUST carry an explicit `name` field — herdsman uses it
        // (not the JS object key) for the attribute-ID→name reverse lookup when
        // parsing read responses. Omitting it makes reads come back keyed
        // "undefined" and leaves every expose null.
        m.deviceAddCustomCluster('mechinationDiag', {
            ID: 0xfc00,
            attributes: {
                faultRaw: {name: 'faultRaw', ID: 0x0000, type: Zcl.DataType.BITMAP8},
                rtdWires: {name: 'rtdWires', ID: 0x0010, type: Zcl.DataType.UINT8},
                rtdNominalOhm: {name: 'rtdNominalOhm', ID: 0x0011, type: Zcl.DataType.UINT16},
                rtdRefOhm: {name: 'rtdRefOhm', ID: 0x0012, type: Zcl.DataType.UINT16},
                filter50hz: {name: 'filter50hz', ID: 0x0013, type: Zcl.DataType.BOOLEAN},
                samplePeriodMs: {name: 'samplePeriodMs', ID: 0x0014, type: Zcl.DataType.UINT16},
                antenna: {name: 'antenna', ID: 0x0020, type: Zcl.DataType.BOOLEAN, write: true},
            },
            commands: {},
            commandsResponse: {},
        }),

        // Primary measurement.
        m.temperature(),

        // RTD fault flag — pushed by the device (standard Binary Input cluster).
        m.binary({
            name: 'rtd_fault',
            cluster: 'genBinaryInput',
            attribute: 'presentValue',
            valueOn: ['FAULT', 1],
            valueOff: ['OK', 0],
            description: 'RTD fault detected by the MAX31865 (open/short/out-of-range)',
            access: 'STATE_GET',
            reporting: {min: 0, max: 3600, change: 0},
            entityCategory: 'diagnostic',
        }),

        // Antenna selection — writable, persisted on the device.
        m.binary({
            name: 'antenna_external',
            cluster: 'mechinationDiag',
            attribute: 'antenna',
            valueOn: ['external_ufl', 1],
            valueOff: ['onboard_ceramic', 0],
            description: 'RF antenna: external U.FL vs on-board ceramic (persisted on device)',
            access: 'ALL',
            entityCategory: 'config',
        }),

        // Read-only diagnostics (refresh to read).
        m.numeric({
            name: 'rtd_fault_raw',
            cluster: 'mechinationDiag',
            attribute: 'faultRaw',
            access: 'STATE_GET',
            description: 'Raw MAX31865 fault byte: 0x80 RTD>hi, 0x40 RTD<lo, 0x20/0x10 REFIN, 0x08 RTDIN, 0x04 OV/UV',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'rtd_wires',
            cluster: 'mechinationDiag',
            attribute: 'rtdWires',
            access: 'STATE_GET',
            description: 'RTD wiring configuration',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'rtd_nominal',
            cluster: 'mechinationDiag',
            attribute: 'rtdNominalOhm',
            unit: 'Ω',
            access: 'STATE_GET',
            description: 'RTD nominal resistance at 0 °C (100 = PT100, 1000 = PT1000)',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'rtd_reference',
            cluster: 'mechinationDiag',
            attribute: 'rtdRefOhm',
            unit: 'Ω',
            access: 'STATE_GET',
            description: 'Reference resistor (430 = PT100 board, 4300 = PT1000 board)',
            entityCategory: 'diagnostic',
        }),
        m.binary({
            name: 'mains_filter_50hz',
            cluster: 'mechinationDiag',
            attribute: 'filter50hz',
            valueOn: ['50Hz', 1],
            valueOff: ['60Hz', 0],
            access: 'STATE_GET',
            description: 'Mains rejection notch filter frequency',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'sample_period',
            cluster: 'mechinationDiag',
            attribute: 'samplePeriodMs',
            unit: 'ms',
            access: 'STATE_GET',
            description: 'Local RTD sampling period',
            entityCategory: 'diagnostic',
        }),

        m.identify(),
    ],
    meta: {},
};
