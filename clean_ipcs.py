import os
import subprocess

# Gestione brutale, elimina tutte le IPC con flag rw 0666
# Controllare che non faccia danni con il comando ipcs prima di eseguire
# Non è da consegnare, ci serve solo per pulire le IPC dopo un'esecuzione

# Elenco tutti gli oggetti IPC del sistema

# Rimuovi i semafori
ipcs_output = subprocess.check_output(['ipcs', '-s']).decode()
lines = ipcs_output.split('\n')
for line in lines:
    parts = line.split()
    if len(parts) >= 3 and parts[3] == '666':
        os.system(f'ipcrm -s {parts[1]}')

# Rimuovi le memorie condivise
ipcs_output = subprocess.check_output(['ipcs', '-m']).decode()
lines = ipcs_output.split('\n')
for line in lines:
    parts = line.split()
    if len(parts) >= 3 and parts[3] == '666':
        os.system(f'ipcrm -m {parts[1]}')

# Rimuovi le code di messaggi
ipcs_output = subprocess.check_output(['ipcs', '-q']).decode()
lines = ipcs_output.split('\n')
for line in lines:
    parts = line.split()
    if len(parts) >= 3 and parts[3] == '666':
        os.system(f'ipcrm -q {parts[1]}')

# Elimina anche gli shared memory segments
ipcs_output = subprocess.check_output(['ipcs', '-m']).decode()
lines = ipcs_output.split('\n')
for line in lines:
    parts = line.split()
    if len(parts) >= 3 and parts[3] == '666':  # Controlla i permessi 0666
        os.system(f'ipcrm -m {parts[1]}')  # Elimina il segmento di memoria condivisa
