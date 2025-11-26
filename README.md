# Capteur Sonore Matter

![Capteur Sonore](img/Capteur.jpg)

Assemblage et programmation

---

Il n’existe actuellement aucun capteur de niveau sonore dans le commerce. Il existe quelques alarmes de détection de pic sonore, mais elles ne correspondent pas au besoin. D’ailleurs, les normes Zigbee et Matter ne prennent pas en charge les capteurs sonores dans leurs modèles de données. Une démarche est à faire auprès de la CSA pour l’intégrer dans les prochaines versions du protocole.

## Assemblage

Pour connecter la carte Arduino Nano Matter et le capteur sonore Grove (réf. 101020023), il faut relier les 3 ports suivants et les souder à l’étain :

- SIG ↔ A0  
- GND ↔ GND  
- VCC ↔ 5V

![Montage](img/Branchement_Capteur.png)

## Programmation

Maintenant que le capteur est assemblé, il faut y mettre le programme. Pour cela, il faut un ordinateur muni du logiciel « Arduino IDE », sur lequel il faut ajouter les modules **Arduino AVR Boards** et **Silicon Labs** (voir captures ci-dessous).

![Arduino Boards 1](img/Capteur%20sonore_html_485a56b6.png) ![Arduino Boards 2](img/Capteur%20sonore_html_f8f30e2b.png)

Une fois ces modules installés, il faut choisir le bon modèle de « Board » et le bon port de connexion.

![Port et configuration](img/Capteur%20sonore_html_25338ff2.png)

Vous pouvez également ouvrir le programme `sonometre_vX.ino`.

Une fois cette étape atteinte, vous pouvez écraser le logiciel de démarrage de la carte en cliquant sur :
`Tools >> Burn Bootloader` pour la préparer à recevoir le programme.

Une fois terminé, vous pouvez lancer le téléversement du programme [Sonometre.ino](Sonometre.ino) sur le capteur.

![Téléversement terminé](img/Capteur%20sonore_html_9e77c984.png)

Le capteur est prêt !

## Appairage

#### Retour visuel

Le capteur clignote en bleu tant qu'il n'est pas connecté au réseau Thread, puis reste bleu une fois la connectivité Thread OK.
Il clignote ensuite en vert tant que la connectivité Matter n'est pas fonctionnelle puis passe au vert fixe un fois connecté.

Pour l’appairer au contrôleur domotique, vous aurez besoin de ce QR Code ou du code d’appairage manuel : **34970112332**

![QR Code d'appairage](img/Capteur%20sonore_html_1de0b1fc.png)

## Étalo­nnage 

Il est nécessaire d'étalonner le capteur une fois positionné à sa place. En effet, certains bruits annexes comme une VMC ou d'autres équipements bruyants doivent être "effacés" pour ne mesurer que le niveau sonore des bruits non persistants. L'étalonnage n'est réalisable que s'il est connecté en Matter.

L'étalonnage se fait en 2 étapes, une première fois avec le niveau de bruit le plus bas possible de la pièce, puis une seconde fois avec le niveau sonore cible (80dB). Pour effectuer ce second étalonnage, il est nécessaire d'avoir une enceinte pruissante, un véritable sonomètre, et de jouer le son de ce lien [Youtube](https://www.youtube.com/watch?v=vccQw3zVEXc) au niveau sonore cible.
<ul>
  <li>Etalonnage bas:
    <ul>
      <li>Appuyer 3 secondes sur le "User Button":
        <ul>
          <li>un clignotement rapide Bleu/Rouge indique la prise en conmpte</li>
          <li>un clignotement Bleu moins rapide annonce que l'étalonnage va commencer</li>
          <li>le voyant passe au Rouge le temps de la mesure (10s)</li>
          <li>un clignotement Bleu annonce que l'étalonnage bas est terminé</li>
        </ul>
      </li>
    </ul>
  </li>
  <li>Etalonnage haut:
    <ul>
      <li>Appuyer 3 secondes sur le "User Button":
        <ul>
          <li>un clignotement rapide Bleu/Rouge indique la prise en conmpte</li>
          <li>un clignotement Bleu moins rapide annonce que l'étalonnage va commencer</li>
          <li>le voyant passe au Rouge le temps de la mesure (10s)</li>
          <li>un clignotement Vert annonce que l'étalonnage est terminé et sauvegardé dans l'EEPROM</li>
        </ul>
      </li>
    </ul>
  </li>
</ul>

Une fois cet étalonnage terminé, la mesure remontée aura une valeur oscillant autour de 20 lorsqu'il n'y a pas de bruit dans la pièce et une mesure oscillant autour de 80 lorsque le niveau de bruit de la pièce atteindra le niveau sonore lors de l'étalonnage haut.

---

