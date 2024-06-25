##### Les versions de file.c
dans le dossier `script/files`
`file12.c` : version de ouichefs par défaut
`file14.c` : version de l'étape 1.4 avec read/write/ioctl
`file17.c` : version de l'étape 1.7 avec read/write/ioctl/defragmentation

##### benchmark
dans le dossier `benchmark`
`benchmark_file.c` : avec structure FILE
`benchmark_fd.c` : avec descripteur de fichiers

dans le dossier `benchmark/data_csv`
Les temps récupérés par différentes versions de read/write en fonction des versions 
`12.csv` : version par défaut
`14.csv` : version de l'étape 1.4 avec read/write
`17.csv` : version de l'étape 1.7 avec read/write

"filesize" : taille du fichier
"position" : position du fichier où il y a les données
"create_time" : création du fichier (est le même pour tous les fichiers de même taille)
"write_time" : écriture à la position
"read_time" : lecture à la position

##### ioctl 
dans le dossier `ioctl`
`user.c <pathfile>` : ioctl pour récupérer les informations de blocs sur le fichiers
`defrag.c <pathfile>` : ioctl pour défragmenter un fichier 

##### documentation
dans le dossier `doc`