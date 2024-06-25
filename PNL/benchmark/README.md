## Préréquis 
- préciser le dossier où les fichiers vont être créés (variable `FOLDER` dans les fichiers .c)
- préciser le pas de taille de fichier à générer (`FILESIZE_PACE` dans les fichiers .c)
- préciser sur combien de x% seront générés les positions (`POSITION_PACE` dans les fichiers .c)
- ajouter à ce dossier un nouveau dossier `test`
- faire la commande `make`

### Pour executer le programme benchmark 
Il existe 2 types de programmes qui font la même chose :
- avec un descripteur de fichier : `benchmark_fd`
- avec la structure FILE en C : `benchmark_file`
(facultatif) le chemin d'un fichier(avec son nom) peut être ajouté (`%s.csv`) à l'exécution du programme pour stocker le temps d'écriture/lecture du fichier.

#### Description du processus de benchmark
- création des fichiers `file_%d`(numéro du fichier généré) dans un dossier `test` de taille variée allant jusqu'à 1Mo en fonction du pas définie.
- ajout de "This a Hello World I/O test in ouichefs" dans une position de pas en pas du fichier
- lecture à la position du fichier
- affichage du temps mis pour la création du fichier, ajout de donnée, et lecture du fichier (peut être stocké dans un fichier si argument mis)

### Pour démarrer les tests 
Executer `benchmark_test.sh`

#### Description du processus des tests 
Nous parcourons les fichiers générés dans le dossier `test`.
Pour chaque fichier :
- lecture de la 1ere ligne qui contient une liste des positions de notre donnée "This a Hello World I/O test in ouichefs" (39 octets)
- lecture des 39 octets à partir des positions lues
- vérifie qu'on a bien "This a Hello World I/O test in ouichefs" à la position

Si un des fichiers n'est pas conforme, la position actuel et la position correcte seront affichés.
Sinon il est indiqué que tous les fichiers sont conformes.

