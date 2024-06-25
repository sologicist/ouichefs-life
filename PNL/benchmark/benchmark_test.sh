#!/bin/bash

# Chemin du dossier à vérifier
folder="./ouichefs/test"
data="This a Hello World I/O test in ouichefs"
no=39

passe=1

# Parcours de tous les fichiers dans le dossier
for file in "$folder"/*; do
    # Vérification si le chemin pointe vers un fichier
    if [ -f "$file" ]; then
        # Lecture de la première ligne pour obtenir les positions
        positions=$(head -n 1 "$file")

        # Vérification si les positions sont valides (séparées par des virgules et contiennent uniquement des chiffres)
        if [[ $positions =~ ^[0-9,]+$ ]]; then
            IFS=',' read -r -a position_array <<< "$positions"
            all_positions_valid=1

            for position in "${position_array[@]}"; do
                # Lecture des 11 prochains octets à partir de la position pour obtenir "Hello World ..."
                content=$(dd if="$file" bs=1 skip=$position count=$no 2>/dev/null)

                # Recherche de la position actuelle de "Hello World" dans le fichier
                actual_position=$(grep -aob "$data" "$file" | grep -E "^$position:" | head -n 1 | cut -d ':' -f 1)

                # Vérification si les 40 octets contiennent "Hello World ..."
                if [ "$content" != "$data" ]; then
                    # Affichage de la position incorrecte et de la position correcte
                    echo "Le fichier $file n'est pas conforme."
                    echo "Position incorrecte: $position"
                    echo "Position actuelle: $actual_position"
                    passe=0
                    all_positions_valid=0
                fi
            done

            if [ "$all_positions_valid" -eq 1 ]; then
                echo "Le fichier $file est conforme."
            fi
        else
            echo "Les positions dans le fichier $file ne sont pas valides."
            passe=0
        fi
    fi
done

if [ "$passe" -eq 1 ]; then
    echo "Tous les fichiers sont conformes !"
else 
    echo "Il y a des fichiers non conformes !"
fi