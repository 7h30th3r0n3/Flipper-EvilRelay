# Legal and responsible use

Flipper-EvilRelay is a security-research and educational project. It demonstrates the NFC
relay attack, a documented class of attack against contactless protocols (ISO 14443-4 /
ISO-DEP), including EMV payments and DESFire access control. It exists so that
researchers, students, pentesters and defenders can understand and reproduce the attack
in a controlled setting and reason about countermeasures.

## What a relay is, and is not

A relay forwards live traffic between a real card and a real reader that are far apart.
The real card must be physically present and powered at the reader side for every
transaction. It does not clone a card, does not store card data, and cannot replay a
transaction later. All card data is relayed unchanged; the only value the emulator sets
itself is the ATS frame-waiting time, so the relay fits inside the reader's timing window.
Remove the real card and everything stops at once.

## Use it only when all of these are true

1. You own the card, or you have the cardholder's explicit, informed consent.
2. You own the reader or terminal, or you have written authorisation to test it, for
   example a signed penetration-test scope.
3. Your testing complies with all applicable laws and regulations in your jurisdiction,
   and with the rules of the card scheme or system owner.

## Do not

Do not relay a payment card, transit card, badge or any credential that is not yours
without the owner's consent. That is fraud or unauthorised access and is illegal in most
jurisdictions. Do not use this against production systems you are not authorised to test.

## Countermeasures

Relay attacks are mitigated by proximity and distance-bounding checks. EMV RRP (Relay
Resistance Protocol) measures round-trip timing and rejects slow, relayed responses.
Transaction and device limits and additional cardholder verification reduce the impact.
Studying the attack is how these defences get built and validated.

## Disclaimer

The author provides this project as is, for lawful research and education only, and is
not responsible for any misuse. By using it you accept full responsibility for your own
actions. If you do not have permission to test a given card and reader, do not use this
software.
