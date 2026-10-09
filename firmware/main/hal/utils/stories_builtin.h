/*
 * SPDX-License-Identifier: MIT
 */
// Original stories for 4-6 year olds, read aloud by the AI one page at a time (see stories.cpp).
// Each page is ~120 words (~50 s read calmly); 5 pages is ~4 minutes.
#pragma once

struct BuiltinStory {
    const char* id;
    const char* title;
    const char* pages[5];
};

inline const BuiltinStory kBuiltinStories[] = {
    {"panda_dormir",
     "O Panda que Nao Conseguia Dormir",
     {"Era uma vez, no alto de uma montanha cheia de bambus, um pandinha chamado Bambu. Bambu era fofinho, "
      "redondinho e adorava brincar o dia inteiro. Ele rolava na grama, subia nas arvores e comia folhinhas "
      "verdinhas ate a barriga ficar cheia. Mas quando a lua aparecia no ceu e todos os bichos iam dormir, Bambu "
      "ficava acordado. Ele fechava os olhos, contava ate tres, e abria os olhos de novo. Nada de sono! Ele virava "
      "para um lado, virava para o outro, e nada. Entao, numa noite bem estrelada, Bambu decidiu: vou descobrir como "
      "os outros bichos conseguem dormir! E saiu, bem devagarinho, pela floresta escura e silenciosa.",
      "O primeiro amigo que Bambu encontrou foi a dona Coruja, sentada num galho alto. Dona Coruja, como voce "
      "dorme?, perguntou Bambu. A coruja abriu os olhos enormes e disse: Ah, pandinha, eu durmo de dia! A noite eu "
      "fico acordada, olhando as estrelas. Bambu pensou, pensou, e respondeu: mas eu gosto de brincar de dia! "
      "A coruja riu um uhuuu baixinho e disse: entao va perguntar ao coelho, ele dorme bem cedinho. Bambu agradeceu "
      "e continuou andando, passinho por passinho, ouvindo os grilos cantarem cri cri cri, e o vento balancar as "
      "folhas, chuá, chuá, bem devagar.",
      "Perto de um buraquinho na terra, Bambu encontrou o Coelho Pipoca, ja de pijama. Coelho, como voce dorme tao "
      "rapido?, perguntou o panda. O coelho bocejou um bocejo enorme: aaaahhh. Eu tomo um copinho de leite morno, "
      "escovo os dentes, e meu dentinho fica brilhando! Depois eu me enrolo no meu cobertor macio. Bambu achou a "
      "ideia otima. Mas ele ainda nao estava com sono. Entao o coelho disse: va ate o lago, a tartaruga Teca sabe "
      "o segredo mais importante de todos. E se enfiou na toca, quentinho, e dormiu na mesma hora. Bambu ficou "
      "curioso: qual seria o segredo da tartaruga?",
      "No lago, a lua brilhava em cima da agua como uma bolinha de prata. La estava a tartaruga Teca, bem "
      "velhinha e bem calma. Teca, qual e o segredo para dormir?, perguntou Bambu. A tartaruga sorriu devagar e "
      "disse: o segredo e respirar como as ondinhas do lago. Puxe o ar bem devagar pelo nariz... e solte bem devagar "
      "pela boca. Vamos juntos? Bambu puxou o ar... e soltou. Puxou o ar... e soltou. E sabe o que aconteceu? Os "
      "ombros dele ficaram moles, as patinhas ficaram pesadas, e um bocejo bem grande saiu da boca dele: aaahhh.",
      "Bambu voltou para casa andando devagarinho. Tomou um golinho de leite morno, escovou os dentes, se enrolou "
      "no cobertor macio, e respirou como as ondinhas do lago. Puxa o ar... solta o ar. La fora, a coruja cantava "
      "baixinho, os grilos faziam cri cri, e a lua cuidava de todos la do alto. Os olhos de Bambu foram ficando "
      "pesados, pesados... e ele sonhou que voava em cima de uma nuvem de algodao. E agora, toda noite, quando a "
      "lua aparece, Bambu lembra do segredo da tartaruga. E voce, quer tentar tambem? Puxa o ar... e solta. "
      "Boa noite, pequenino. Fim."}},

    {"abobora_brilhar",
     "A Abobora que Queria Brilhar",
     {"Numa horta cheia de cores, perto de um sitio pequenininho, morava uma abobora chamada Abigail. Ela era "
      "redonda, laranja e tinha um cabinho verde enroladinho no alto da cabeca. Todas as outras aboboras eram "
      "grandes e gordinhas, mas Abigail era pequena, do tamanho de uma bola de futebol. Toda noite, ela olhava para "
      "o ceu e via as estrelas piscando, os vaga-lumes brilhando e a lua iluminando tudo. E Abigail suspirava: ah, "
      "como eu queria brilhar tambem! Mas aboboras nao brilham, diziam as outras aboboras, rindo. Abobora e para "
      "fazer sopa e doce! Abigail ficava triste, mas nunca parava de sonhar.",
      "Um dia, chegou outubro, e o sitio ficou diferente. As criancas da casa comecaram a pendurar morcegos de papel, "
      "teias de mentirinha e chapeus de bruxa. E Abigail ouviu uma palavra que nunca tinha escutado: Halloween! "
      "O que e Halloween?, perguntou ela para o espantalho Zeca, que vigiava a horta. O espantalho, que sabia de "
      "tudo, respondeu: e uma festa de fantasias, de doces e de brincadeiras. E sabe o que as criancas mais gostam? "
      "De escolher uma abobora especial para enfeitar a porta. Abigail arregalou os olhos. Sera que alguem ia "
      "escolher uma abobora tao pequenininha como ela?",
      "Na manha seguinte, duas criancas entraram na horta, de maos dadas. Elas olharam a abobora grandona: muito "
      "pesada! Olharam a abobora comprida: muito torta! E entao viram Abigail, pequena e redondinha, brilhando de "
      "laranja no sol. Essa!, gritaram juntas. Essa e perfeita! Abigail quase pulou de alegria. As criancas levaram "
      "ela para a cozinha e, com a ajuda de um adulto, desenharam no rosto dela dois olhos de triangulo e um sorriso "
      "enorme cheio de dentinhos. Abigail se olhou no espelho e riu: que sorriso mais engracado! Mas ela ainda nao "
      "brilhava.",
      "Quando a noite chegou, as criancas colocaram Abigail na porta de casa. Entao a mamae trouxe uma luzinha e "
      "colocou bem dentro dela, com muito cuidado. E sabe o que aconteceu? Uma luz amarela e quentinha saiu pelos "
      "olhos e pelo sorriso de Abigail! Ela estava brilhando! Brilhando como as estrelas, como os vaga-lumes, como a "
      "lua! As criancas fantasiadas passavam pela rua e paravam para olhar: olha que abobora linda! Os morcegos de "
      "verdade davam voltinhas em cima dela, e ate a coruja do vizinho veio ver. Abigail nunca tinha se sentido tao "
      "feliz.",
      "La da horta, as outras aboboras viram a luz. Olhem, e a Abigail!, disseram, admiradas. Ela esta brilhando! "
      "E o espantalho Zeca sorriu: eu sempre disse que ela era especial. Naquela noite, Abigail aprendeu uma coisa "
      "importante: nao importa ser grande ou pequeno. Quando a gente tem um sorriso bonito e uma luz por dentro, a "
      "gente brilha do nosso jeitinho. E todo ano, quando chega outubro, as criancas lembram da abobora pequenina que "
      "iluminou a porta da casa. E voce? Qual e a luz que voce tem ai dentro? Feliz Halloween! Fim."}},

    {"morcego_escuro",
     "Lolo, o Morcego que Tinha Medo do Escuro",
     {"Dentro de uma caverna enorme, cheia de pedras brilhantes, morava uma familia de morcegos. Tinha o papai "
      "morcego, a mamae morcego e o filhotinho, que se chamava Lolo. Lolo tinha orelhas grandes, asas fininhas e um "
      "segredo que ninguem sabia: ele tinha medo do escuro! Isso era um problema, porque morcegos acordam quando "
      "a noite chega. Todos os morcegos saiam voando felizes quando o sol ia embora, mas Lolo ficava escondido no "
      "cantinho mais claro da caverna, perto de uma pedra que brilhava um pouquinho. E quando a mamae chamava, Lolo "
      "dizia: ja vou, mamae! Mas nao ia.",
      "Uma noite, a mamae morcego percebeu que Lolo estava tremendo. Ela pendurou de cabeca para baixo do lado dele, "
      "como os morcegos gostam, e perguntou: o que foi, meu filho? Lolo cochichou: mamae, eu tenho medo do escuro. "
      "La fora e tudo preto, e eu nao vejo nada! A mamae fez um carinho nas orelhas dele e disse: sabe, Lolo, os "
      "morcegos tem um superpoder. A gente nao precisa ver com os olhos. A gente ve com as orelhas! Lolo ficou "
      "confuso: ver com as orelhas? Como assim? E a mamae sorriu: venha, eu vou te mostrar.",
      "Os dois foram ate a entrada da caverna. La fora, a noite estava escura como chocolate. Agora, faca um "
      "barulhinho, disse a mamae. Lolo fez um chiado bem fininho: iiii! E o som voou pela noite, bateu numa arvore "
      "e voltou para as orelhas dele: iiii! E de repente, Lolo sentiu na cabeca o desenho da arvore, com seus "
      "galhos e folhas! Ele fez outro chiado: iiii! E sentiu uma pedra, um riozinho e uma flor. Uau!, disse Lolo. "
      "Eu estou vendo com as orelhas! O escuro nao estava mais tao escuro assim.",
      "Lolo bateu as asas e voou, primeiro baixinho, depois mais alto. Iiii, e la estava o riozinho. Iiii, e la "
      "estava o caminho entre as arvores. Ele encontrou outros filhotes de morcego brincando de pega-pega no ar, e "
      "brincou junto. Descobriu que a noite tinha um monte de coisas lindas: vaga-lumes piscando como luzinhas de "
      "festa, sapos cantando cua cua no lago, e a lua grandona sorrindo la no alto. Quanta coisa ele estava perdendo "
      "escondido no cantinho da caverna! Lolo riu tanto que ate esqueceu que tinha medo.",
      "Quando o sol comecou a aparecer, Lolo voltou para casa, cansado e feliz. Pendurou de cabeca para baixo, do "
      "lado da mamae, e disse: mamae, o escuro nao e tao assustador. Ele so e diferente. A mamae deu um beijo nele "
      "e respondeu: todo mundo tem medo de alguma coisa, meu filho. Ser corajoso e sentir medo e tentar mesmo assim. "
      "E Lolo dormiu sorrindo, sonhando com a proxima noite de aventuras. E voce, tem medo de alguma coisa? Lembre do "
      "Lolo: as vezes, o que parece assustador so precisa ser conhecido de outro jeito. Fim."}},

    {"girafa_chapeu",
     "Gigi, a Girafa, e o Chapeu Voador",
     {"Na savana, onde o capim e amarelinho e o sol esquenta tudo, morava uma girafa chamada Gigi. Gigi tinha o "
      "pescoco mais comprido de todos, pintinhas marrons pelo corpo e um chapeu vermelho de que ela gostava muito. "
      "Era um chapeu redondo, com uma flor amarela do lado, que a vovo girafa tinha dado de presente. Gigi usava o "
      "chapeu todo dia: para comer folhas, para passear, e ate para dormir. Mas um dia, veio um vento forte, muito "
      "forte, fuuuu! E o chapeu vermelho voou da cabeca de Gigi, subindo, subindo, la para o ceu azul!",
      "Meu chapeu!, gritou Gigi. E saiu correndo atras dele, com aquelas pernas compridas, tum, tum, tum. O chapeu "
      "voou por cima de um rio, e Gigi encontrou o hipopotamo Hugo tomando banho. Hugo, voce viu meu chapeu?, "
      "perguntou ela. O hipopotamo abriu a boca enorme e disse: vi sim, foi para la, para os lados das arvores! "
      "E sabe o que mais? Eu vou com voce! E la foram os dois: a girafa correndo e o hipopotamo pulando pelo "
      "caminho, ploft, ploft, todo molhado e feliz.",
      "Perto das arvores, eles encontraram um grupo de macaquinhos fazendo a maior bagunca. E adivinha o que um "
      "dos macaquinhos estava usando na cabeca? O chapeu vermelho de Gigi! O macaquinho fazia caretas e dancava, e "
      "todos riam. Ei, esse chapeu e meu!, disse Gigi. O macaquinho parou e ficou vermelho de vergonha, igualzinho "
      "ao chapeu. Desculpa, disse ele, eu achei no chao e achei tao bonito! Gigi pensou um pouquinho. O macaquinho "
      "parecia tao feliz com o chapeu.",
      "Entao Gigi teve uma ideia. Que tal a gente brincar todo mundo junto?, perguntou ela. Cada um usa o chapeu "
      "um pouquinho! O macaquinho pulou de alegria. E assim comecou a brincadeira do chapeu voador: o macaquinho "
      "usava e dancava, depois o hipopotamo Hugo usava e cantava, depois um passarinho usava e piava, e por ultimo "
      "Gigi usava e fazia a pose mais elegante da savana. Ate o leao, que era muito serio, quis usar o chapeu, e "
      "todo mundo riu quando ele ficou com a flor amarela caida no nariz.",
      "Quando o sol foi se pondo, deixando o ceu laranja e rosa, o macaquinho devolveu o chapeu para Gigi. Obrigado "
      "por dividir, disse ele. Gigi colocou o chapeu de volta na cabeca e sorriu: obrigada a voces, eu nunca me "
      "diverti tanto! Naquela noite, Gigi contou para a vovo girafa toda a aventura. E a vovo disse: as coisas que "
      "a gente divide ficam ainda mais especiais. E desde aquele dia, todo sabado, a savana inteira se reune para a "
      "brincadeira do chapeu voador. E voce, o que gosta de dividir com os amigos? Fim."}},

    {"robo_estrela",
     "O Robo e a Estrela Cadente",
     {"Numa cidade cheia de predios e luzes, no quarto de uma crianca, morava um robozinho chamado Tico. Tico tinha "
      "olhos que piscavam em azul, bracos de mola e uma antena na cabeca que fazia bip bip quando ele ficava feliz. "
      "Tico ajudava em tudo: arrumava os brinquedos, contava historias e ate cantava musicas de ninar. Mas tinha "
      "uma coisa que Tico queria muito, muito mesmo: ele queria ver uma estrela de perto. Toda noite, ele ficava na "
      "janela olhando o ceu, mas as luzes da cidade eram tao fortes que quase nenhuma estrela aparecia.",
      "Uma noite, aconteceu uma coisa incrivel. Um risco de luz atravessou o ceu, rapido como um foguete: fiuuum! "
      "Era uma estrela cadente! E ela caiu, bem devagarinho, no jardim da casa, brilhando entre as flores. A antena "
      "de Tico fez bip bip bip sem parar! Ele desceu as escadas com cuidado para nao acordar ninguem, abriu a porta "
      "e foi ate o jardim. La estava ela: uma estrelinha pequena, do tamanho de uma maca, brilhando amarelo. Mas ela "
      "estava chorando. Snif, snif.",
      "Por que voce esta chorando, estrelinha?, perguntou Tico. A estrelinha respondeu: eu me chamo Luma. Eu estava "
      "brincando de escorregar no ceu e cai! Agora eu nao sei voltar para casa, e la em cima minha familia deve estar "
      "preocupada. Tico pensou, pensou, e a antena dele girou: bip! Eu tenho uma ideia, disse ele. Eu sou um robo, e "
      "robos sabem construir coisas. Vou construir algo para te levar de volta! Luma parou de chorar e sorriu, e o "
      "jardim inteiro ficou mais claro.",
      "Tico juntou tudo que encontrou: uma caixa de papelao, rolhas, elasticos, um guarda-chuva velho e muitas "
      "pecinhas de lego. Montou, encaixou, colou e apertou. E no final, tinha construido um foguete! Era meio torto "
      "e meio colorido, mas era um foguete. Luma sentou dentro dele, e Tico deu a contagem: tres, dois, um... ja! "
      "O foguete subiu, subiu, mas la no meio do caminho comecou a cair! Tico ficou triste. Entao Luma brilhou com "
      "toda a forca, e a luz dela empurrou o foguete para cima, cada vez mais alto!",
      "La no ceu, Luma encontrou sua familia de estrelas, que piscavam de alegria. E antes de ir, ela gritou la de "
      "cima: obrigada, Tico! Voce e o melhor amigo que uma estrela pode ter! Desde aquela noite, quando Tico olha "
      "pela janela, ha uma estrelinha que pisca mais forte que todas as outras, so para ele: pisca, pisca. E a "
      "antena dele responde: bip, bip. Porque amigos de verdade continuam amigos, mesmo quando estao longe. E se "
      "voce olhar o ceu hoje a noite, talvez veja a Luma piscando para voce tambem. Boa noite. Fim."}},

    {"narnia",
     "Narnia: O Leao, a Feiticeira e o Guarda-Roupa",
     {"Era uma vez quatro irmaos: Pedro, o mais velho, Susana, Edmundo e a pequena Lucia. Eles foram passar uns "
      "tempos numa casa enorme no campo, cheia de corredores e quartos misteriosos. Num dia de chuva, brincando de "
      "esconde-esconde, Lucia entrou num guarda-roupa antigo, cheio de casacos de pele. Ela foi andando para o "
      "fundo, mais e mais, e de repente sentiu algo gelado nos pes: era neve! Lucia tinha chegado num lugar magico "
      "chamado Narnia, uma floresta coberta de neve, com um poste de luz brilhando no meio das arvores.",
      "La, Lucia conheceu o senhor Tumnus, um fauno, metade homem e metade bode, muito gentil, que tomou cha com "
      "ela. Ele contou um segredo triste: em Narnia era sempre inverno e nunca Natal, por causa de uma rainha "
      "malvada, a Feiticeira Branca. Quando Lucia voltou e contou tudo, os irmaos nao acreditaram. Mas um dia, os "
      "quatro entraram juntos no guarda-roupa e chegaram a Narnia. Edmundo, porem, ja tinha encontrado a Feiticeira "
      "antes. Ela deu doces magicos para ele e prometeu que ele seria um principe, se trouxesse os irmaos ate o "
      "castelo dela.",
      "Os irmaos encontraram o casal de castores, que os levou para casa e contou uma noticia maravilhosa: Aslam "
      "estava voltando! Aslam era um leao grande, dourado e muito bondoso, o verdadeiro rei de Narnia. Mas, enquanto "
      "todos conversavam, Edmundo escapuliu e foi ate o castelo de gelo da Feiticeira. La ele descobriu que ela nao "
      "era nada boazinha e ficou preso. Enquanto isso, a neve comecou a derreter, as flores brotaram e ate o Papai "
      "Noel apareceu, trazendo presentes para Pedro, Susana e Lucia. O inverno estava acabando, porque Aslam estava "
      "chegando!",
      "Pedro, Susana e Lucia encontraram Aslam num acampamento cheio de criaturas amigas. Aslam mandou salvar "
      "Edmundo, e Edmundo pediu desculpas aos irmaos, que o abracaram com carinho. Mas a Feiticeira apareceu e "
      "disse que, pelas leis antigas de Narnia, Edmundo pertencia a ela. Entao Aslam, com muita coragem, ofereceu a "
      "si mesmo no lugar de Edmundo. Naquela noite, Susana e Lucia viram tudo, muito tristes, e choraram ao lado "
      "do leao. Mas Aslam conhecia uma magia ainda mais antiga e mais forte: a magia do amor de quem se oferece para "
      "salvar alguem.",
      "Quando o sol nasceu, Aslam voltou a viver, mais forte e brilhante do que nunca! As meninas pularam de alegria "
      "e subiram nas costas dele. Aslam soprou sobre as estatuas de pedra que a Feiticeira tinha feito, e todos "
      "voltaram a se mexer. Juntos, eles venceram a Feiticeira Branca, e Narnia ficou livre para sempre. Os quatro "
      "irmaos viraram reis e rainhas de Narnia e viveram muitas aventuras. E aprenderam que o perdao e a coragem "
      "sao mais fortes que qualquer feitico. E voce, quem voce levaria com voce para Narnia? Fim."}},

};
