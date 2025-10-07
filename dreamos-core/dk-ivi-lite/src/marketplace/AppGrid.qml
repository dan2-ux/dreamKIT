// Copyright (c) 2025 Eclipse Foundation.
// 
// This program and the accompanying materials are made available under the
// terms of the MIT License which is available at
// https://opensource.org/licenses/MIT.
// 
// SPDX-License-Identifier: MIT
import QtQuick 2.15
import QtQuick.Controls 2.15

GridView {
    id: appGrid
    // bind this from the page
    property var modelData
    // your C++ QAbstractListModel
    model: modelData
    clip: true
    cellWidth:  Math.floor(width / Math.floor(width / 200))
    cellHeight: 280

    // helper for selection + notifying the page
    signal appSelected(int index)

    function setActiveIndex(i) {
        currentIndex = i
        appSelected(i)
    }

    delegate: Item {
        width:  appGrid.cellWidth
        height: appGrid.cellHeight

        // roles from C++:
        //   name, author, iconUrl, isInstalled, installingIndex
        visible: name && iconUrl

        Rectangle {
            id: card
            anchors.centerIn: parent
            width:  parent.width  - 16
            height: parent.height - 16
            radius: 20
            border.width: 1
            border.color: appGrid.currentIndex===index ? "#00D4AA" : "#4c2b2b"
            color:       appGrid.currentIndex===index ? "#00D4AA15" : "#1A1A1A"

            Column {
                anchors.fill: parent; anchors.margins: 20; spacing: 12

                // ICON + FALLBACK + BADGE
                Rectangle {
                    width: 100; height: 100; radius: 20; color: "#FFFFFF"
                    anchors.horizontalCenter: parent.horizontalCenter

                    Image {
                        id: icon
                        source: iconUrl      // role from C++
                        width: 80; height: 80
                        anchors.centerIn: parent
                        fillMode: Image.PreserveAspectFit
                        Rectangle {
                            anchors.fill: parent
                            visible: status===Image.Error
                            color: "#E3F2FD"; radius: 16
                            Text {
                                anchors.centerIn: parent
                                text: name.charAt(0).toUpperCase()
                                font.pixelSize: 32; font.bold: true; color: "#1976D2"
                            }
                        }
                    }

                    // Installed badge
                    Rectangle {
                        visible: isInstalled    // role from C++
                        width: 20; height: 20; radius: 10
                        color: "#00D4AA"
                        anchors.top: parent.top; anchors.right: parent.right
                        anchors.margins: -8
                        Text {
                            anchors.centerIn: parent
                            text: "✓"; color: "white"; font.pixelSize: 10; font.bold: true
                        }
                    }
                }

                // NAME & AUTHOR
                Column {
                    width: parent.width; spacing: 4
                    Text {
                        text: name; font.pixelSize: 16; font.bold: true; color: "#FFFFFF"
                        font.family: "Segoe UI"; elide: Text.ElideRight
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        text: author; font.pixelSize: 12; color: "#90FFFFFF"
                        font.family: "Segoe UI"; elide: Text.ElideRight
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                    }
                }

                // Install / Installed button
                Rectangle {
                    width: parent.width; height: 36; radius: 18
                    color: isInstalled ? "#00D4AA" : "#2196F3"
                    Text {
                        anchors.centerIn: parent
                        text: isInstalled ? qsTr("Installed") : qsTr("Install")
                        color: "white"; font.family: "Segoe UI"; font.pixelSize: 12
                    }
                }
            }

            // LOCAL BUSY OVERLAY if this card is installing
            Rectangle {
                anchors.fill: parent
                color: "#2196F3"
                opacity: (index === vm.installingIndex && vm.isInstalling) ? 0.5 : 0
                visible: index === vm.installingIndex && vm.isInstalling

                BusyIndicator {
                    running: visible
                    anchors.centerIn: parent
                    width: 60
                    height: 60

                    contentItem: Item {
                        implicitWidth: 60
                        implicitHeight: 60

                        Item {
                            id: busyItem
                            anchors.fill: parent
                            opacity: parent.parent.running ? 1 : 0

                            RotationAnimator {
                                target: busyItem
                                running: busyItem.parent.parent.visible && busyItem.parent.parent.running
                                from: 0
                                to: 360
                                loops: Animation.Infinite
                                duration: 1250
                            }

                            Repeater {
                                id: busyRepeater
                                model: 6

                                Rectangle {
                                    x: busyItem.width / 2 - width / 2
                                    y: busyItem.height / 2 - height / 2
                                    implicitWidth: 10
                                    implicitHeight: 10
                                    radius: 5
                                    color: "#00D4AA"
                                    transform: [
                                        Translate {
                                            y: -Math.min(busyItem.width, busyItem.height) * 0.5 + 5
                                        },
                                        Rotation {
                                            angle: index / busyRepeater.count * 360
                                            origin.x: 5
                                            origin.y: 5
                                        }
                                    ]
                                    opacity: 1.0 - index / busyRepeater.count * 0.7
                                }
                            }
                        }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: appGrid.setActiveIndex(index)
        }
    }
}
