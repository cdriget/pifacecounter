# pifacecounter
Count PiFace Inputs and insert values into MySQL


Fonctionnement actuel pour la mesure de l'énergie en Wh :

- Sur le Raspberry PI, le programme pifacecounter (langage C compilé) tourne en tâche de fond (démon) => démarrage automatique au boot via les scripts /etc/init.d/
- Ce programme s'appuie sur les bibliothèques libmcp23s17 et libpifacedigital.
- Lorsque pifacecounter démarre, il initialise la connexion SPI sur la carte d'extension Piface.
- Puis il active les interruptions, et démarre un second thread.
- Ce thread réagit immédiatement à chaque interruption, c'est à dire à chaque fois que le statut de l'une des 8 entrées de la Piface change. Cela permet d'incrémenter un compteur (en fait c'est un tableau de 8 compteurs, car il y a 8 interfaces à compter).
- Pendant ce temps là, la boucle principale entre dans une boucle infinie, et attend 60 secondes à chaque itération. Lorsque le délai est écoulé, elle prend les 8 valeurs stockées dans le tableau, et les injecte dans MySQL, puis remet à 0 les 8 compteurs. L'injection dans MySQL a été décrite quelques posts plus haut, à l'aide de la commande INSERT INTO qui stocke le nombre d'impulsions comptées durant l’intervalle de 60s, et incrémente le cumul total.


Evolutions à venir, et particulièrement le calcul de la puissance instantanée en W :

- Je ne vais pas m'appuyer sur la DB SQL, qui n'est là que pour historiser les données afin d'effectuer les calculs de coûts financiers à long terme.
- Grâce au thread dédié aux interruptions, pifacecounter connait précisément le timestamp de chaque impulsion sur chaque entrée. Il suffit d'utiliser une variable de type tableau avec 8 timestamps.
- Donc au centième de seconde près, il pourra calculer la puissance instantanée, que ce soit de très faibles valeurs de style 0.1W ou de très fortes valeurs du style plusieurs kW.
- Cette puissance pourra être envoyé en Push (via une requête HTTP) vers une box domotique, mais aussi stockée dans la DB, dans une table (la même que pour l'énergie, ou une table dédiée au puissances afin de ne pas mélanger des données ayant des unités différentes).
- Afin de ne pas surcharger le PI et le réseau, en cas de forte consommation électrique, les impulsions s'enchainent à un rythme effrayant (plusieurs Wh par secondes), donc il faudra gérer un délai minimal pour le Push (1 seconde sera OK je pense), et aussi n'envoyer le Push qu'en cas de changement de puissance significative (par exemple supérieure à 20%). Bref, il s'agit de recréer les paramètres de mises à jour que l'on trouve sur un Fibaro Wall Plug par exemple.
- Cette gestion du Push sera gérée par un 3ème thread, afin de ne pas bloquer le thread de comptage d'impulsion en cas de latence réseau élevée.
