* Prueba visual de BalloonTip GDI+ para VFP9 (32 bits).
* Copiar BalloonTipTest.dll junto a este PRG antes de ejecutarlo.
DECLARE INTEGER GetActiveWindow IN user32
DECLARE INTEGER BT_Show IN BalloonTipTest.dll ;
    INTEGER hOwner, INTEGER nX, INTEGER nY, ;
    STRING cTitle, STRING cText, INTEGER nTimeout
DECLARE INTEGER BT_Hide IN BalloonTipTest.dll
DECLARE INTEGER BT_Destroy IN BalloonTipTest.dll

PUBLIC goBalloonTest
goBalloonTest = CREATEOBJECT("frmBalloonTest")
goBalloonTest.Show()
READ EVENTS
IF VARTYPE(goBalloonTest) = "O"
    goBalloonTest.Release()
ENDIF
RELEASE goBalloonTest
RETURN

DEFINE CLASS frmBalloonTest AS Form
    Caption = "Prueba BalloonTip GDI+"
    Width = 620
    Height = 260
    AutoCenter = .T.
    ShowWindow = 0
    PROCEDURE Activate
        IF This.Tag <> "MOSTRADO"
            This.Tag = "MOSTRADO"
            =BT_Show(This.HWnd, 22, 48, "Atención", ;
                "El bloqueo de mayúsculas está activado", 8000)
        ENDIF
    ENDPROC
    PROCEDURE Destroy
        =BT_Destroy()
        CLEAR EVENTS
    ENDPROC
ENDDEFINE
